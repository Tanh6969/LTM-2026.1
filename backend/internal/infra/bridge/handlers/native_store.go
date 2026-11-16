package handlers

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"regexp"
	"strings"
	"sync"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgconn"
	"github.com/jackc/pgx/v5/pgxpool"
	"golang.org/x/crypto/bcrypt"
	"ltm-flight/internal/infra/bridge"
)

var emailPattern = regexp.MustCompile(`^[^\s@]+@[^\s@]+\.[^\s@]+$`)
var phonePattern = regexp.MustCompile(`^[0-9]{10}$`)

type NativeStore struct{ Pool *pgxpool.Pool; mu sync.Mutex; cache map[string]searchCache }
type searchCache struct { until time.Time; data []map[string]any }

type profile struct {
	ID int64 `json:"id"`
	Email string `json:"email"`
	FirstName string `json:"firstName"`
	LastName string `json:"lastName"`
	Phone string `json:"phoneNumber"`
	Gender string `json:"gender"`
	BirthDate string `json:"dateOfBirth"`
	Identification string `json:"identificationNumber"`
	Address string `json:"address"`
	Role string `json:"role"`
}

func validPassword(s string) bool {
	if len(s) < 8 || len(s) > 72 { return false }
	letter, digit := false, false
	for _, r := range s {
		if r >= '0' && r <= '9' { digit = true }
		if r >= 'A' && r <= 'Z' || r >= 'a' && r <= 'z' { letter = true }
	}
	return letter && digit
}

func (n *NativeStore) Register(ctx context.Context, first, last, email, password, phone string) (profile, error) {
	email = strings.ToLower(strings.TrimSpace(email))
	if !emailPattern.MatchString(email) || !validPassword(password) || !phonePattern.MatchString(phone) || strings.TrimSpace(first) == "" || strings.TrimSpace(last) == "" {
		return profile{}, bridge.NewError(bridge.StatusBadRequest, "INVALID_REGISTRATION")
	}
	hash, err := bcrypt.GenerateFromPassword([]byte(password), 10)
	if err != nil { return profile{}, err }
	var p profile
	err = n.Pool.QueryRow(ctx, `INSERT INTO Users(email,hashed_password,first_name,last_name,phone_number,role)
		VALUES($1,$2,$3,$4,$5,'customer') RETURNING user_id,email,first_name,last_name,phone_number,role`,
		email, string(hash), strings.TrimSpace(first), strings.TrimSpace(last), phone).
		Scan(&p.ID,&p.Email,&p.FirstName,&p.LastName,&p.Phone,&p.Role)
	var pe *pgconn.PgError
	if errors.As(err, &pe) && pe.Code == "23505" { return profile{}, bridge.NewError(bridge.StatusConflict, "EMAIL_EXISTS") }
	return p, err
}

func (n *NativeStore) Login(ctx context.Context, email, password string) (profile, string, error) {
	email = strings.ToLower(strings.TrimSpace(email))
	if !emailPattern.MatchString(email) || password == "" { return profile{}, "", bridge.NewError(bridge.StatusBadRequest, "INVALID_CREDENTIALS") }
	// Serialize attempts for one email, including simultaneous connections.
	tx, err := n.Pool.Begin(ctx)
	if err != nil { return profile{}, "", err }
	defer tx.Rollback(ctx)
	if _, err = tx.Exec(ctx, `SELECT pg_advisory_xact_lock(hashtextextended($1, 0))`, email); err != nil { return profile{}, "", err }
	var failures int
	if err = tx.QueryRow(ctx, `SELECT count(*) FROM LoginAttempts WHERE email=$1 AND attempted_at > now()-interval '15 minutes'`, email).Scan(&failures); err != nil { return profile{}, "", err }
	if failures >= 5 { return profile{}, "", bridge.NewError(bridge.StatusUnauthorized, "ACCOUNT_TEMP_LOCKED") }
	var p profile
	var hash string
	err = tx.QueryRow(ctx, `SELECT user_id,email,coalesce(first_name,''),coalesce(last_name,''),coalesce(phone_number,''),role::text,hashed_password
		FROM Users WHERE lower(email)=$1 AND is_active=true AND deleted_at IS NULL`, email).
		Scan(&p.ID,&p.Email,&p.FirstName,&p.LastName,&p.Phone,&p.Role,&hash)
	if err != nil && !errors.Is(err,pgx.ErrNoRows) { return profile{}, "", err }
	if err != nil || bcrypt.CompareHashAndPassword([]byte(hash), []byte(password)) != nil {
		if _, err = tx.Exec(ctx, `INSERT INTO LoginAttempts(email) VALUES($1)`, email); err != nil { return profile{}, "", err }
		if err = tx.Commit(ctx); err != nil { return profile{}, "", err }
		return profile{}, "", bridge.NewError(bridge.StatusUnauthorized, "INVALID_CREDENTIALS")
	}
	var bytes [32]byte
	if _, err = rand.Read(bytes[:]); err != nil { return profile{}, "", err }
	id := hex.EncodeToString(bytes[:])
	if _, err = tx.Exec(ctx, `DELETE FROM LoginAttempts WHERE email=$1`, email); err != nil { return profile{}, "", err }
	if _, err = tx.Exec(ctx, `INSERT INTO Sessions(session_id,user_id,expires_at) VALUES($1,$2,now()+interval '30 days')`, id,p.ID); err != nil { return profile{}, "", err }
	if err = tx.Commit(ctx); err != nil { return profile{}, "", err }
	return p,id,nil
}

func (n *NativeStore) Resume(ctx context.Context, id string) (profile,error) {
	var p profile
	if len(id)!=64 { return p,bridge.NewError(bridge.StatusUnauthorized,"INVALID_SESSION") }
	if _,err:=hex.DecodeString(id);err!=nil{return p,bridge.NewError(bridge.StatusUnauthorized,"INVALID_SESSION")}
	err:=n.Pool.QueryRow(ctx, `UPDATE Sessions s SET last_seen_at=now() FROM Users u
		WHERE s.session_id=$1 AND s.user_id=u.user_id AND s.expires_at>now() AND u.is_active=true AND u.deleted_at IS NULL
		RETURNING u.user_id,u.email,coalesce(u.first_name,''),coalesce(u.last_name,''),coalesce(u.phone_number,''),u.role::text`,id).
		Scan(&p.ID,&p.Email,&p.FirstName,&p.LastName,&p.Phone,&p.Role)
	if errors.Is(err,pgx.ErrNoRows){return p,bridge.NewError(bridge.StatusUnauthorized,"INVALID_SESSION")}
	return p,err
}

func (n *NativeStore) Logout(ctx context.Context,id string) error {
	_,err:=n.Pool.Exec(ctx,`DELETE FROM Sessions WHERE session_id=$1`,id)
	return err
}

func (n *NativeStore) Profile(ctx context.Context,uid int64) (profile,error) {
	var p profile
	err:=n.Pool.QueryRow(ctx,`SELECT user_id,email,coalesce(first_name,''),coalesce(last_name,''),coalesce(phone_number,''),
		gender::text,coalesce(date_of_birth::text,''),coalesce(identification_number,''),coalesce(address,''),role::text
		FROM Users WHERE user_id=$1`,uid).Scan(&p.ID,&p.Email,&p.FirstName,&p.LastName,&p.Phone,&p.Gender,&p.BirthDate,&p.Identification,&p.Address,&p.Role)
	return p,err
}

func (n *NativeStore) Update(ctx context.Context,uid int64,p profile) (profile,error) {
	if !phonePattern.MatchString(p.Phone) || (p.Gender!="Male" && p.Gender!="Female" && p.Gender!="Other") {return profile{},bridge.NewError(bridge.StatusBadRequest,"INVALID_PROFILE")}
	var birth any
	if p.BirthDate!="" { d,e:=time.Parse("2006-01-02",p.BirthDate);if e!=nil{return profile{},bridge.NewError(bridge.StatusBadRequest,"INVALID_BIRTH_DATE")};birth=d }
	_,err:=n.Pool.Exec(ctx,`UPDATE Users SET first_name=$2,last_name=$3,phone_number=$4,gender=$5,date_of_birth=$6,
		identification_number=$7,address=$8 WHERE user_id=$1`,uid,strings.TrimSpace(p.FirstName),strings.TrimSpace(p.LastName),p.Phone,p.Gender,birth,p.Identification,p.Address)
	if err!=nil{return profile{},err}
	return n.Profile(ctx,uid)
}

func (n *NativeStore) ChangePassword(ctx context.Context,uid int64,current,old,newPassword string) error {
	if !validPassword(newPassword){return bridge.NewError(bridge.StatusBadRequest,"INVALID_PASSWORD")}
	tx,err:=n.Pool.Begin(ctx);if err!=nil{return err};defer tx.Rollback(ctx)
	var hash string
	if err=tx.QueryRow(ctx,`SELECT hashed_password FROM Users WHERE user_id=$1 FOR UPDATE`,uid).Scan(&hash);err!=nil{return err}
	if bcrypt.CompareHashAndPassword([]byte(hash),[]byte(old))!=nil{return bridge.NewError(bridge.StatusUnauthorized,"INVALID_OLD_PASSWORD")}
	newHash,err:=bcrypt.GenerateFromPassword([]byte(newPassword),10);if err!=nil{return err}
	if _,err=tx.Exec(ctx,`UPDATE Users SET hashed_password=$2 WHERE user_id=$1`,uid,string(newHash));err!=nil{return err}
	if _,err=tx.Exec(ctx,`DELETE FROM Sessions WHERE user_id=$1 AND session_id<>$2`,uid,current);err!=nil{return err}
	return tx.Commit(ctx)
}
