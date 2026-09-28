import React from "react";
import AdminNavbar from "@/components/admin/navbar";

export default function AdminLayout({ children }) {
  return (
    <div className="min-h-screen bg-gray-50">
      <AdminNavbar />
      <main className="p-6">{children}</main>
    </div>
  );
}
