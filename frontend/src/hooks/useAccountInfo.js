import { useState } from "react";

export function useAccountInfo() {
  const [personalInfo, setPersonalInfo] = useState({
    name: "Nguyễn Tuấn Anh",
    email: "tuananh@example.com",
    phone: "0912345678",
    avatar: "/AvatarUser/giang.jpg",
    bookingHistory: [],
  });
  const [isEditing, setIsEditing] = useState(false);
  const [loading, setLoading] = useState(false);
  const [errorMessage, setErrorMessage] = useState("");

  const handleUpdate = (updatedData) => {
    setPersonalInfo((prev) => ({ ...prev, ...updatedData }));
    setIsEditing(false);
  };

  return {
    personalInfo,
    setPersonalInfo,
    isEditing,
    setIsEditing,
    loading,
    errorMessage,
    handleUpdate,
  };
}

export default useAccountInfo;
