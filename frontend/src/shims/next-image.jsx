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
  fill,
  ...props
}) {
  const isFill = Boolean(fill || layout === 'fill');
  const fillStyle = isFill
    ? {
        position: 'absolute',
        top: 0,
        left: 0,
        width: '100%',
        height: '100%',
        objectFit: objectFit || 'cover',
      }
    : {};

  const mergedStyle = {
    ...fillStyle,
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
