import React, { lazy, Suspense } from 'react';

export default function dynamic(importer, options = {}) {
  const LazyComponent = lazy(importer);

  return function DynamicComponent(props) {
    return (
      <Suspense fallback={options.loading ? <options.loading /> : null}>
        <LazyComponent {...props} />
      </Suspense>
    );
  };
}
