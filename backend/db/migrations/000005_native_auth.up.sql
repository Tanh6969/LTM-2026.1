CREATE TABLE Sessions (
  session_id TEXT PRIMARY KEY,
  user_id BIGINT NOT NULL REFERENCES Users(user_id) ON DELETE CASCADE,
  created_at timestamptz NOT NULL DEFAULT now(),
  expires_at timestamptz NOT NULL,
  last_seen_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX sessions_user_id_idx ON Sessions(user_id);
CREATE INDEX sessions_expires_at_idx ON Sessions(expires_at);

CREATE TABLE LoginAttempts (
  email TEXT NOT NULL,
  attempted_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX login_attempts_email_time_idx ON LoginAttempts(email, attempted_at);
