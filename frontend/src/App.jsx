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
