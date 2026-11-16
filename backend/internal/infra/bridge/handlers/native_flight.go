package handlers

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"
	"time"

	"ltm-flight/internal/infra/bridge"
)

func registerNativeFlight(r *bridge.Router,n *NativeStore) {
	r.Handle(bridge.MsgFlightSearch,bridge.AuthNone,func(ctx context.Context,s *bridge.Session,raw json.RawMessage)(any,error){
		var q struct { From string `json:"departureCity"`; To string `json:"arrivalCity"`; Date string `json:"flightDate"`; Class string `json:"class"`; Adults int `json:"adults"`; Children int `json:"children"`; Infants int `json:"infants"` }
		if err:=decode(raw,&q);err!=nil{return nil,err}
		if q.Class==""{q.Class="economy"}
		if q.Adults==0 && q.Children==0 {q.Adults=1}
		date,err:=time.Parse("2006-01-02",q.Date)
		if err!=nil || q.From=="" || q.To=="" || q.From==q.To || (q.Class!="economy"&&q.Class!="business"&&q.Class!="firstClass") || q.Adults<1 || q.Children<0 || q.Infants<0 || q.Adults+q.Children>9 || q.Infants>q.Adults {return nil,badRequest("INVALID_SEARCH")}
		key:=fmt.Sprintf("%s|%s|%s|%s|%d|%d|%d",strings.ToLower(q.From),strings.ToLower(q.To),q.Date,q.Class,q.Adults,q.Children,q.Infants)
		n.mu.Lock();cached,ok:=n.cache[key];n.mu.Unlock()
		if ok && time.Now().Before(cached.until){return map[string]any{"data":cached.data},nil}
		rows,err:=n.Pool.Query(ctx,`SELECT f.flight_id,f.flight_number,a.name,coalesce(f.departure_city,''),coalesce(f.arrival_city,''),
			f.departure_time,f.arrival_time,fa.class::text,fa.price,
			(fa.seats_total - COALESCE((SELECT count(*) FROM Tickets t LEFT JOIN Bookings b ON b.booking_id=t.booking_id
				WHERE t.flight_id=f.flight_id AND t.flight_class=fa.class AND
				(t.status IN ('Active','CheckedIn') OR (b.hold_expires_at>now() AND b.hold_expires_at IS NOT NULL AND t.status<>'Cancelled'))),0))::int
			FROM Flights f JOIN FlightFares fa ON fa.flight_id=f.flight_id JOIN Airlines a ON a.airline_id=f.airline_id
			WHERE (lower(f.departure_city)=lower($1) OR lower(f.departure_airport)=lower($1))
			AND (lower(f.arrival_city)=lower($2) OR lower(f.arrival_airport)=lower($2))
			AND (f.departure_time AT TIME ZONE 'Asia/Ho_Chi_Minh')::date=$3
			AND f.departure_time>now()+interval '2 hours' AND f.status<>'Cancelled' AND fa.class::text=$4
			AND fa.seats_total - COALESCE((SELECT count(*) FROM Tickets t LEFT JOIN Bookings b ON b.booking_id=t.booking_id
				WHERE t.flight_id=f.flight_id AND t.flight_class=fa.class AND
				(t.status IN ('Active','CheckedIn') OR (b.hold_expires_at>now() AND b.hold_expires_at IS NOT NULL AND t.status<>'Cancelled'))),0) >= $5
			ORDER BY f.departure_time`,q.From,q.To,date,q.Class,q.Adults+q.Children)
		if err!=nil{return nil,err};defer rows.Close()
		items:=make([]map[string]any,0)
		for rows.Next(){var id int64;var number,airline,from,to,class string;var departure,arrival time.Time;var price,seats int
			if err:=rows.Scan(&id,&number,&airline,&from,&to,&departure,&arrival,&class,&price,&seats);err!=nil{return nil,err}
			items=append(items,map[string]any{"flightId":fmt.Sprint(id),"flightNumber":number,"airline":airline,"departureCity":from,"arrivalCity":to,"departureTime":departure.Format(time.RFC3339),"arrivalTime":arrival.Format(time.RFC3339),"class":class,"basePrice":price,"seatsLeft":seats})
		}
		if err:=rows.Err();err!=nil{return nil,err}
		n.mu.Lock();if n.cache==nil{n.cache=make(map[string]searchCache)};if len(n.cache)>256{for k,v:=range n.cache{if time.Now().After(v.until){delete(n.cache,k)}};if len(n.cache)>256{n.cache=make(map[string]searchCache)}};n.cache[key]=searchCache{time.Now().Add(30*time.Second),items};n.mu.Unlock()
		return map[string]any{"data":items},nil
	})
	r.Handle(bridge.MsgAirportList,bridge.AuthNone,func(ctx context.Context,s *bridge.Session,raw json.RawMessage)(any,error){
		rows,err:=n.Pool.Query(ctx,`SELECT DISTINCT city,airport FROM (
			SELECT departure_city AS city,departure_airport AS airport FROM Flights
			UNION SELECT arrival_city,arrival_airport FROM Flights) x WHERE city IS NOT NULL AND airport IS NOT NULL ORDER BY city,airport`)
		if err!=nil{return nil,err};defer rows.Close()
		items:=make([]map[string]string,0);for rows.Next(){var city,airport string;if err:=rows.Scan(&city,&airport);err!=nil{return nil,err};items=append(items,map[string]string{"city":city,"airport":airport})}
		return map[string]any{"data":items},rows.Err()
	})
}
