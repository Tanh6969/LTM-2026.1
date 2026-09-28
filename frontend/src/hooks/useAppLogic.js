import { useState, useEffect } from "react";

export function useAppLogic(router) {
  const [showScrollTopButton, setShowScrollTopButton] = useState(false);

  useEffect(() => {
    const checkScrollTop = () => {
      if (window.scrollY > 300) {
        setShowScrollTopButton(true);
      } else {
        setShowScrollTopButton(false);
      }
    };
    window.addEventListener("scroll", checkScrollTop);
    return () => window.removeEventListener("scroll", checkScrollTop);
  }, []);

  const handleScrollToTop = () => {
    window.scrollTo({ top: 0, behavior: "smooth" });
  };

  return {
    showScrollTopButton,
    handleScrollToTop,
  };
}

export default useAppLogic;
