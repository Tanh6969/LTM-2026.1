-- Phải tách khỏi 000004: PostgreSQL không cho dùng giá trị enum vừa thêm trong cùng transaction.
ALTER TYPE booking_status ADD VALUE IF NOT EXISTS 'expired';

ALTER TYPE ticket_status ADD VALUE IF NOT EXISTS 'Changed';

ALTER TYPE ticket_status ADD VALUE IF NOT EXISTS 'CheckedIn';
