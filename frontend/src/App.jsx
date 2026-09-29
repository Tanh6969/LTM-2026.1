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
import FlightBooking from "@/pages/flights/index";
import FlightLoading from "@/pages/flights/loading";
import CheckInPage from "@/pages/check-in/index";
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
    window.dispatchEvent(new PopStateEvent("popstate"));
    setCurrentPath(path);
  };

  const renderPage = () => {
    const path = currentPath.split("?")[0];
    switch (path) {
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
      case "/check-in":
        return <CheckInPage />;
      case "/flights":
        return <FlightBooking />;
      case "/flights/loading":
        return <FlightLoading />;
      default:
        if (path.startsWith("/booking-management")) {
          return <FlightBookingPage />;
        }
        if (path.startsWith("/check-in")) {
          return <CheckInPage />;
        }
        if (path.startsWith("/flights/loading")) {
          return <FlightLoading />;
        }
        if (path.startsWith("/flights")) {
          return <FlightBooking />;
        }
        return <Custom404 />;
    }
  };

  const isAuthPage = currentPath === "/login" || currentPath === "/signup";

  return (
    <AuthProvider>
      {/* Floating Demo Navigation Toolbar */}
      <div className="fixed bottom-4 left-1/2 -translate-x-1/2 z-[9999] bg-white/95 backdrop-blur-md px-4 py-2 rounded-full shadow-2xl border border-gray-200 flex items-center gap-1.5 text-xs font-medium max-w-[95vw] overflow-x-auto">
        <span className="text-gray-500 font-bold px-1 hidden md:inline shrink-0">Trang thử nghiệm:</span>
        <button
          onClick={() => navigateTo("/")}
          className={`px-3 py-1.5 rounded-full transition-all shrink-0 ${
            currentPath === "/"
              ? "bg-[#e8604c] text-white shadow-sm font-semibold"
              : "hover:bg-gray-100 text-gray-700"
          }`}
        >
          Trang chủ
        </button>
        <button
          onClick={() => navigateTo("/flights")}
          className={`px-3 py-1.5 rounded-full transition-all shrink-0 ${
            currentPath.startsWith("/flights")
              ? "bg-[#e8604c] text-white shadow-sm font-semibold"
              : "hover:bg-gray-100 text-gray-700"
          }`}
        >
          Chuyến bay
        </button>
        <button
          onClick={() => navigateTo("/check-in")}
          className={`px-3 py-1.5 rounded-full transition-all shrink-0 ${
            currentPath.startsWith("/check-in")
              ? "bg-[#e8604c] text-white shadow-sm font-semibold"
              : "hover:bg-gray-100 text-gray-700"
          }`}
        >
          Check-in
        </button>
        <button
          onClick={() => navigateTo("/booking-management")}
          className={`px-3 py-1.5 rounded-full transition-all shrink-0 ${
            currentPath.startsWith("/booking-management")
              ? "bg-[#e8604c] text-white shadow-sm font-semibold"
              : "hover:bg-gray-100 text-gray-700"
          }`}
        >
          Quản lý đặt chỗ
        </button>
        <button
          onClick={() => navigateTo("/contact")}
          className={`px-3 py-1.5 rounded-full transition-all shrink-0 ${
            currentPath === "/contact"
              ? "bg-[#e8604c] text-white shadow-sm font-semibold"
              : "hover:bg-gray-100 text-gray-700"
          }`}
        >
          Liên hệ
        </button>
        <button
          onClick={() => navigateTo("/my-account")}
          className={`px-3 py-1.5 rounded-full transition-all shrink-0 ${
            currentPath === "/my-account"
              ? "bg-[#e8604c] text-white shadow-sm font-semibold"
              : "hover:bg-gray-100 text-gray-700"
          }`}
        >
          Tài khoản
        </button>
        <button
          onClick={() => navigateTo("/login")}
          className={`px-3 py-1.5 rounded-full transition-all shrink-0 ${
            currentPath === "/login"
              ? "bg-[#e8604c] text-white shadow-sm font-semibold"
              : "hover:bg-gray-100 text-gray-700"
          }`}
        >
          Đăng nhập
        </button>
        <button
          onClick={() => navigateTo("/signup")}
          className={`px-3 py-1.5 rounded-full transition-all shrink-0 ${
            currentPath === "/signup"
              ? "bg-[#e8604c] text-white shadow-sm font-semibold"
              : "hover:bg-gray-100 text-gray-700"
          }`}
        >
          Đăng ký
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
