-- PostgreSQL không hỗ trợ xóa giá trị khỏi enum. Các giá trị thêm vào ('expired', 'Changed', 'CheckedIn')
-- vô hại nếu còn tồn tại, nên down là no-op.
SELECT 1;
