import React, { createContext, useContext, useState, useEffect } from "react";

const AuthContext = createContext({
  isAuthenticated: false,
  user: null,
  login: () => {},
  logout: () => {},
});

export function AuthProvider({ children }) {
  const [isAuthenticated, setIsAuthenticated] = useState(false);
  const [user, setUser] = useState({
    name: "Tuấn Anh",
    email: "tuananh@example.com",
    avatar: "/AvatarUser/giang.jpg",
  });

  useEffect(() => {
    if (typeof window !== "undefined") {
      const token = localStorage.getItem("token");
      if (token) {
        setIsAuthenticated(true);
      }
    }
  }, []);

  const login = (tokenOrUser) => {
    setIsAuthenticated(true);
    if (typeof tokenOrUser === "string") {
      localStorage.setItem("token", tokenOrUser);
    } else if (tokenOrUser && typeof tokenOrUser === "object") {
      if (tokenOrUser.token) {
        localStorage.setItem("token", tokenOrUser.token);
      }
      setUser((prev) => ({ ...prev, ...tokenOrUser }));
    }
  };

  const logout = () => {
    setIsAuthenticated(false);
    if (typeof window !== "undefined") {
      localStorage.removeItem("token");
    }
  };

  return (
    <AuthContext.Provider value={{ isAuthenticated, user, login, logout }}>
      {children}
    </AuthContext.Provider>
  );
}

export function useAuth() {
  return useContext(AuthContext);
}

export default AuthContext;
