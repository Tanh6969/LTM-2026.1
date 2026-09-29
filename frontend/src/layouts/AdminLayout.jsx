import AdminNavbar from "@/components/admin/navbar";

const AdminLayout = ({ children }) => {
  return (
    <div className="flex flex-row relative min-h-screen bg-gray-50">
      <div className="fixed top-0 left-0 h-screen z-40">
        <AdminNavbar />
      </div>
      <main className="ml-64 flex-1 w-full min-h-screen p-6">
        {children}
      </main>
    </div>
  );
};

export default AdminLayout;
