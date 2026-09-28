import { useState } from "react";

export function useSignup(onSuccess) {
  const [formData, setFormData] = useState({
    firstName: "",
    lastName: "",
    email: "",
    phone: "",
    password: "",
    confirmPassword: "",
  });
  const [loading, setLoading] = useState(false);
  const [errorMessage, setErrorMessage] = useState("");

  const handleInputChange = (e) => {
    const { name, value } = e.target;
    setFormData((prev) => ({ ...prev, [name]: value }));
  };

  const handleSubmit = (e) => {
    e.preventDefault();
    if (formData.password !== formData.confirmPassword) {
      setErrorMessage("Mật khẩu xác nhận không khớp!");
      return;
    }
    setLoading(true);
    setErrorMessage("");
    setTimeout(() => {
      setLoading(false);
      if (onSuccess) onSuccess();
    }, 500);
  };

  return { formData, loading, errorMessage, handleInputChange, handleSubmit };
}

export default useSignup;
