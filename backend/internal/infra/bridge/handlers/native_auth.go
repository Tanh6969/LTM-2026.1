package handlers

import (
	"context"
	"encoding/json"

	"ltm-flight/internal/infra/bridge"
)

func registerNativeAuth(r *bridge.Router,n *NativeStore) {
	r.Handle(bridge.MsgAuthRegister,bridge.AuthNone,func(ctx context.Context,s *bridge.Session,raw json.RawMessage)(any,error){
		var q struct{ First string `json:"first_name"`; Last string `json:"last_name"`; Email string `json:"email"`; Password string `json:"password"`; Phone string `json:"phone_number"` }
		if err:=decode(raw,&q);err!=nil{return nil,err}
		p,err:=n.Register(ctx,q.First,q.Last,q.Email,q.Password,q.Phone)
		if err!=nil{return nil,err}
		return map[string]any{"user":p},nil
	})
	r.Handle(bridge.MsgAuthLogin,bridge.AuthNone,func(ctx context.Context,s *bridge.Session,raw json.RawMessage)(any,error){
		var q struct{Email string `json:"email"`;Password string `json:"password"`}
		if err:=decode(raw,&q);err!=nil{return nil,err}
		p,id,err:=n.Login(ctx,q.Email,q.Password);if err!=nil{return nil,err}
		s.SetIdentity(p.ID,p.Role);s.SetSessionID(id)
		return map[string]any{"user":map[string]any{"id":p.ID,"name":p.FirstName+" "+p.LastName,"role":p.Role},"sessionId":id},nil
	})
	r.Handle(bridge.MsgAuthResume,bridge.AuthNone,func(ctx context.Context,s *bridge.Session,raw json.RawMessage)(any,error){
		var q struct{SessionID string `json:"sessionId"`}
		if err:=decode(raw,&q);err!=nil{return nil,err}
		p,err:=n.Resume(ctx,q.SessionID);if err!=nil{return nil,err}
		s.SetIdentity(p.ID,p.Role);s.SetSessionID(q.SessionID)
		return map[string]any{"user":map[string]any{"id":p.ID,"name":p.FirstName+" "+p.LastName,"role":p.Role},"sessionId":q.SessionID},nil
	})
	r.Handle(bridge.MsgAuthLogout,bridge.AuthUser,func(ctx context.Context,s *bridge.Session,_ json.RawMessage)(any,error){
		if err:=n.Logout(ctx,s.SessionID());err!=nil{return nil,err}
		s.SetSessionID("");s.ClearIdentity()
		return map[string]string{"message":"logged out"},nil
	})
	r.Handle(bridge.MsgAuthProfile,bridge.AuthUser,func(ctx context.Context,s *bridge.Session,_ json.RawMessage)(any,error){
		uid,_:=s.Identity();return n.Profile(ctx,uid)
	})
	r.Handle(bridge.MsgAuthUpdate,bridge.AuthUser,func(ctx context.Context,s *bridge.Session,raw json.RawMessage)(any,error){
		var q profile;if err:=decode(raw,&q);err!=nil{return nil,err}
		uid,_:=s.Identity();return n.Update(ctx,uid,q)
	})
	r.Handle(bridge.MsgAuthPassword,bridge.AuthUser,func(ctx context.Context,s *bridge.Session,raw json.RawMessage)(any,error){
		var q struct{Old string `json:"oldPassword"`;New string `json:"newPassword"`}
		if err:=decode(raw,&q);err!=nil{return nil,err}
		uid,_:=s.Identity();if err:=n.ChangePassword(ctx,uid,s.SessionID(),q.Old,q.New);err!=nil{return nil,err}
		return map[string]string{"message":"password changed"},nil
	})
}

