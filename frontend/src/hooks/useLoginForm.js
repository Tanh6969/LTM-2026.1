import { useState } from "react";

export function useLogin(onSuccess) {
  const [formData, setFormData] = useState({ email: "", password: "" });
  const [loading, setLoading] = useState(false);
  const [errorMessage, setErrorMessage] = useState("");

  const handleInputChange = (e) => {
    const { name, value } = e.target;
    setFormData((prev) => ({ ...prev, [name]: value }));
  };

  const handleSubmit = (e) => {
    e.preventDefault();
    setLoading(true);
    setErrorMessage("");
    setTimeout(() => {
      setLoading(false);
      if (onSuccess) onSuccess();
    }, 500);
  };

  return { formData, loading, errorMessage, handleInputChange, handleSubmit };
}

export default useLogin;
