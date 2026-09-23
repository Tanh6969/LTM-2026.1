// pages/index.js
import React from "react";
import Hero from "//wsl.localhost/Ubuntu/home/tanh/Projects/Qairlines/frontend/src/components/hero";
import Destination from "//wsl.localhost/Ubuntu/home/tanh/Projects/Qairlines/frontend/src/components/destination";
import About from "//wsl.localhost/Ubuntu/home/tanh/Projects/Qairlines/frontend/src/components/about";
import Review from "//wsl.localhost/Ubuntu/home/tanh/Projects/Qairlines/frontend/src/components/review";
import Benefit from "//wsl.localhost/Ubuntu/home/tanh/Projects/Qairlines/frontend/src/components/benefit";
import AOS from "aos";
import "aos/dist/aos.css";
import { useEffect } from "react";

function Home() {
  useEffect(() => {
    AOS.init({
      duration: 1500,
    });
  }, []);
  return (
    <div>
      <Hero />
      <Destination />
      <About />
      <Review />
      <Benefit />
    </div>
  );
}

export default Home;
