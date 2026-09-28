import { useState } from "react";

export default function useFlightSearch() {
  const [fromAirport, setFromAirport] = useState("HAN");
  const [toAirport, setToAirport] = useState("SGN");
  const [departureDate, setDepartureDate] = useState(new Date());
  const [returnDate, setReturnDate] = useState(null);
  const [tripType, setTripType] = useState("one-way");

  const swapAirports = () => {
    const temp = fromAirport;
    setFromAirport(toAirport);
    setToAirport(temp);
  };

  const isValid = Boolean(fromAirport && toAirport && departureDate);

  return {
    fromAirport,
    setFromAirport,
    toAirport,
    setToAirport,
    departureDate,
    setDepartureDate,
    returnDate,
    setReturnDate,
    tripType,
    setTripType,
    swapAirports,
    isValid,
  };
}
