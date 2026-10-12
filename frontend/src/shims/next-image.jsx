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

  // Handle object src (imported images)
  const resolvedSrc =
    typeof src === 'object' && src !== null
      ? (src.default || src.src || '')
      : src;

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

  const layoutStyle = {};
  if (layout === 'responsive') {
    layoutStyle.width = '100%';
    layoutStyle.height = 'auto';
  } else if (layout === 'intrinsic') {
    layoutStyle.maxWidth = '100%';
    layoutStyle.height = 'auto';
  }

  const mergedStyle = {
    ...fillStyle,
    ...layoutStyle,
    ...style,
    ...(objectFit && !isFill ? { objectFit } : {}),
  };

  return (
    <img
      src={resolvedSrc}
      alt={alt}
      {...(!isFill && width ? { width } : {})}
      {...(!isFill && height ? { height } : {})}
      className={className}
      style={mergedStyle}
      loading={priority ? 'eager' : 'lazy'}
      {...props}
    />
  );
}
