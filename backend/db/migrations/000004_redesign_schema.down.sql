-- Đảo ngược 000004 (dữ liệu của các bảng/cột mới sẽ mất; Customers/Admins được tạo lại rỗng)

DROP TABLE IF EXISTS TicketChanges;

DROP TABLE IF EXISTS Notifications;

DROP TABLE IF EXISTS FlightStatusHistory;

DROP TABLE IF EXISTS Payments;

-- Tickets
DROP INDEX IF EXISTS uq_tickets_active_seat;

ALTER TABLE
  Tickets DROP CONSTRAINT IF EXISTS tickets_seat_id_fkey;

DELETE FROM
  Tickets
WHERE
  seat_id IS NULL;

ALTER TABLE
  Tickets
ALTER COLUMN
  seat_id
SET
  NOT NULL,
ADD
  CONSTRAINT tickets_seat_id_fkey FOREIGN KEY (seat_id) REFERENCES Seats(seat_id) ON DELETE CASCADE,
  DROP COLUMN ticket_code;

-- Seats
ALTER TABLE
  Seats DROP CONSTRAINT IF EXISTS seats_flight_id_seat_code_key;

ALTER TABLE
  Seats
ADD
  COLUMN is_available BOOLEAN NOT NULL DEFAULT TRUE;

UPDATE
  Seats
SET
  is_available = (status = 'available');

ALTER TABLE
  Seats DROP COLUMN status,
  DROP COLUMN held_until,
  DROP COLUMN held_by_booking,
ALTER COLUMN
  flight_id DROP NOT NULL,
ALTER COLUMN
  seat_code TYPE VARCHAR(3),
ALTER COLUMN
  seat_code
SET
  DEFAULT '1A',
ADD
  CONSTRAINT seats_seat_id_flight_id_key UNIQUE (seat_id, flight_id);

-- Bookings
DROP INDEX IF EXISTS idx_bookings_pnr_email;

DROP INDEX IF EXISTS idx_bookings_user_id;

ALTER TABLE
  Bookings
ADD
  COLUMN user_email VARCHAR(255);

UPDATE
  Bookings
SET
  user_email = contact_email;

ALTER TABLE
  Bookings DROP COLUMN user_id,
  DROP COLUMN contact_name,
  DROP COLUMN contact_email,
  DROP COLUMN contact_phone,
  DROP COLUMN pnr,
  DROP COLUMN total_amount,
  DROP COLUMN hold_expires_at,
ALTER COLUMN
  departure_flight_id DROP NOT NULL;

CREATE INDEX idx_bookings_user_id ON Bookings (user_email);

-- Flights
DROP INDEX IF EXISTS idx_flights_route_time;

DROP INDEX IF EXISTS idx_flights_airline_id;

ALTER TABLE
  Flights
ADD
  COLUMN airline VARCHAR(100),
ADD
  COLUMN base_price INT NOT NULL DEFAULT 0;

UPDATE
  Flights f
SET
  airline = a.name
FROM
  Airlines a
WHERE
  a.airline_id = f.airline_id;

UPDATE
  Flights f
SET
  base_price = ff.price
FROM
  FlightFares ff
WHERE
  ff.flight_id = f.flight_id
  AND ff.class = 'economy';

ALTER TABLE
  Flights DROP COLUMN airline_id;

DROP TABLE IF EXISTS FlightFares;

DROP TABLE IF EXISTS Airlines;

-- News / Users
ALTER TABLE
  News DROP CONSTRAINT IF EXISTS news_author_id_fkey;

DROP TRIGGER IF EXISTS set_updated_at ON Users;

-- Customers / Admins (rỗng)
CREATE TABLE Customers (
  user_id BIGINT PRIMARY KEY REFERENCES Users(user_id) ON DELETE CASCADE,
  phone_number VARCHAR(20),
  gender gender_type NOT NULL DEFAULT 'Other',
  date_of_birth DATE,
  passport_number VARCHAR(50),
  identification_number VARCHAR(50),
  address TEXT,
  loyalty_points INT DEFAULT 0 CHECK (loyalty_points >= 0)
);

CREATE TABLE Admins (
  user_id BIGINT PRIMARY KEY REFERENCES Users(user_id) ON DELETE CASCADE
);

DROP TYPE IF EXISTS notification_type;

DROP TYPE IF EXISTS payment_status;

DROP TYPE IF EXISTS payment_method;

DROP TYPE IF EXISTS seat_status;
