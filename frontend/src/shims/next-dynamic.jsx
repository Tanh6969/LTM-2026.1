import React, { lazy, Suspense } from 'react';

export default function dynamic(importer, options = {}) {
  const LazyComponent = lazy(async () => {
    const mod = await importer();
    const Component = mod.default?.default || mod.default || mod;
    return { default: Component };
  });

  return function DynamicComponent(props) {
    return (
      <Suspense fallback={options?.loading ? <options.loading /> : null}>
        <LazyComponent {...props} />
      </Suspense>
    );
  };
}
