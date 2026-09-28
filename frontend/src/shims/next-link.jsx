import React from 'react';

export default function Link({ href = '#', children, className, onClick, ...props }) {
  const handleClick = (e) => {
    if (onClick) onClick(e);
    // If external link or anchor, let default behavior happen
    if (href.startsWith('http') || href.startsWith('#')) return;
    // Dispatch custom navigation event so App router handles it without full reload
    if (!e.defaultPrevented) {
      e.preventDefault();
      window.history.pushState({}, '', href);
      window.dispatchEvent(new PopStateEvent('popstate'));
    }
  };

  return (
    <a href={href} className={className} onClick={handleClick} {...props}>
      {children}
    </a>
  );
}
