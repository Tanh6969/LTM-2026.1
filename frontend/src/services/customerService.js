
const API_BASE_URL = process.env.NEXT_PUBLIC_API_BASE_URL || "";

export async function fetchCustomerInfo() {
  const token = typeof window !== "undefined" ? localStorage.getItem("token") : null;

  if (!API_BASE_URL || !token) {
    // Trả về dữ liệu mẫu mặc định nếu chưa kết nối backend
    return {
      name: "Nguyễn Tuấn Anh",
      email: "tuananh@example.com",
      phone: "0912345678",
      avatar: "/AvatarUser/giang.jpg",
      gender: "male",
      dob: "2000-01-01",
      address: "Hà Nội, Việt Nam",
      bookingHistory: [],
    };
  }

  const response = await fetch(`${API_BASE_URL}/api/customer/info`, {
    headers: {
      Authorization: `Bearer ${token}`,
    },
  });

  if (!response.ok) {
    throw new Error("Không thể tải thông tin khách hàng từ server");
  }

  const result = await response.json();
  return result.data || result;
}

export async function updateCustomerInfo(updatedInfo) {
  const token = typeof window !== "undefined" ? localStorage.getItem("token") : null;

  if (!API_BASE_URL || !token) {
    console.log("Mock updateCustomerInfo:", updatedInfo);
    return updatedInfo;
  }

  const response = await fetch(`${API_BASE_URL}/api/customer/update`, {
    method: "PUT",
    headers: {
      "Content-Type": "application/json",
      Authorization: `Bearer ${token}`,
    },
    body: JSON.stringify(updatedInfo),
  });

  if (!response.ok) {
    throw new Error("Không thể cập nhật thông tin");
  }

  return await response.json();
}

export default { fetchCustomerInfo, updateCustomerInfo };
