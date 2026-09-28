import React, { useState, useEffect } from "react";
import { AuthProvider } from "@/context/AuthContext";
import MainLayout from "@/layouts/MainLayout";
import { Toaster } from "@/components/ui/toaster";

// Import pages
import Home from "@/pages/index";
import LoginForm from "@/pages/login";
import SignupForm from "@/pages/signup";
import TrangLienHe from "@/pages/contact";
import AccountPage from "@/pages/my-account";
import FlightBookingPage from "@/pages/booking-management/index";
import Custom404 from "@/pages/404";

export default function App() {
  const [currentPath, setCurrentPath] = useState(
    window.location.pathname || "/"
  );

  useEffect(() => {
    const handlePopState = () => {
      setCurrentPath(window.location.pathname || "/");
    };
    window.addEventListener("popstate", handlePopState);
    return () => window.removeEventListener("popstate", handlePopState);
  }, []);

  const navigateTo = (path) => {
    window.history.pushState({}, "", path);
    setCurrentPath(path);
  };

  const renderPage = () => {
    switch (currentPath) {
      case "/":
        return <Home />;
      case "/login":
        return <LoginForm />;
      case "/signup":
        return <SignupForm />;
      case "/contact":
        return <TrangLienHe />;
      case "/my-account":
        return <AccountPage />;
      case "/booking-management":
        return <FlightBookingPage />;
      default:
        // Also support if path starts with
        if (currentPath.startsWith("/booking-management")) {
          return <FlightBookingPage />;
        }
        return <Custom404 />;
    }
  };

  const isAuthPage = currentPath === "/login" || currentPath === "/signup";

  return (
    <AuthProvider>
      {/* Floating Demo Navigation Toolbar */}
      <div className="fixed bottom-4 left-1/2 -translate-x-1/2 z-[9999] bg-white/95 backdrop-blur-md px-4 py-2 rounded-full shadow-2xl border border-gray-200 flex items-center gap-2 text-xs font-medium">
        <span className="text-gray-500 font-bold px-1 hidden sm:inline">Trang thử nghiệm:</span>
        <button
          onClick={() => navigateTo("/")}
          className={`px-3 py-1.5 rounded-full transition-all ${
            currentPath === "/"
              ? "bg-[#e8604c] text-white shadow-sm font-semibold"
              : "hover:bg-gray-100 text-gray-700"
          }`}
        >
          Trang chủ
        </button>
        <button
          onClick={() => navigateTo("/login")}
          className={`px-3 py-1.5 rounded-full transition-all ${
            currentPath === "/login"
              ? "bg-[#e8604c] text-white shadow-sm font-semibold"
              : "hover:bg-gray-100 text-gray-700"
          }`}
        >
          Đăng nhập
        </button>
        <button
          onClick={() => navigateTo("/signup")}
          className={`px-3 py-1.5 rounded-full transition-all ${
            currentPath === "/signup"
              ? "bg-[#e8604c] text-white shadow-sm font-semibold"
              : "hover:bg-gray-100 text-gray-700"
          }`}
        >
          Đăng ký
        </button>
        <button
          onClick={() => navigateTo("/contact")}
          className={`px-3 py-1.5 rounded-full transition-all ${
            currentPath === "/contact"
              ? "bg-[#e8604c] text-white shadow-sm font-semibold"
              : "hover:bg-gray-100 text-gray-700"
          }`}
        >
          Liên hệ
        </button>
        <button
          onClick={() => navigateTo("/my-account")}
          className={`px-3 py-1.5 rounded-full transition-all ${
            currentPath === "/my-account"
              ? "bg-[#e8604c] text-white shadow-sm font-semibold"
              : "hover:bg-gray-100 text-gray-700"
          }`}
        >
          Tài khoản
        </button>
        <button
          onClick={() => navigateTo("/booking-management")}
          className={`px-3 py-1.5 rounded-full transition-all ${
            currentPath.startsWith("/booking-management")
              ? "bg-[#e8604c] text-white shadow-sm font-semibold"
              : "hover:bg-gray-100 text-gray-700"
          }`}
        >
          Quản lý đặt chỗ
        </button>
      </div>

      {/* Main Page Content */}
      {isAuthPage ? (
        renderPage()
      ) : (
        <MainLayout>{renderPage()}</MainLayout>
      )}

      <Toaster />
    </AuthProvider>
  );
}
