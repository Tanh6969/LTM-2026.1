import React, { createContext, useContext, useState } from "react";

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

  const login = (userData) => {
    setIsAuthenticated(true);
    if (userData) setUser(userData);
  };

  const logout = () => {
    setIsAuthenticated(false);
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
