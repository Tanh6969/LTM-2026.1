import { useState, useRef } from "react";

export function useFlightBooking(bookingID, options = {}) {
  const [bookingData, setBookingData] = useState({
    id: bookingID || "QA123456",
    status: "CONFIRMED",
    passenger: "Nguyễn Tuấn Anh",
    flightCode: "VN123",
  });
  const [departureTicketData, setDepartureTicketData] = useState(null);
  const [returnTicketData, setReturnTicketData] = useState(null);
  const [departureFlightData, setDepartureFlightData] = useState(null);
  const [returnFlightData, setReturnFlightData] = useState(null);
  const [selectedTicket, setSelectedTicket] = useState(null);
  const [dialogOpen, setDialogOpen] = useState(false);
  const [error, setError] = useState(null);
  const ticketRef = useRef(null);

  const handleViewTicket = (ticket) => {
    setSelectedTicket(ticket);
    setDialogOpen(true);
  };

  const handleDownload = () => {
    window.print();
  };

  const handleCancelTicket = () => {
    if (options.onCancelSuccess) options.onCancelSuccess();
  };

  return {
    bookingData,
    departureTicketData,
    returnTicketData,
    departureFlightData,
    returnFlightData,
    selectedTicket,
    dialogOpen,
    setDialogOpen,
    error,
    ticketRef,
    handleViewTicket,
    handleDownload,
    handleCancelTicket,
  };
}

export default useFlightBooking;
