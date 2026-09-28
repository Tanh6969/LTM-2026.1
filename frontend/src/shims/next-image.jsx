import React from 'react';

export default function Image({
  src,
  alt = '',
  width,
  height,
  className = '',
  style = {},
  priority,
  layout,
  objectFit,
  ...props
}) {
  const mergedStyle = {
    ...style,
    ...(objectFit ? { objectFit } : {}),
  };

  return (
    <img
      src={src}
      alt={alt}
      width={width}
      height={height}
      className={className}
      style={mergedStyle}
      loading={priority ? 'eager' : 'lazy'}
      {...props}
    />
  );
}
