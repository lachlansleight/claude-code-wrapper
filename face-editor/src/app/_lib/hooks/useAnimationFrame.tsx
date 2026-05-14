// Based off a tweet and codesandbox:
// https://mobile.twitter.com/hieuhlc/status/1164369876825169920
import React, { useCallback, useEffect, useRef } from "react";

const useAnimationFrame = (
    callback: (e: { time: number; delta: number }) => void,
    deps: React.DependencyList
): void => {
    const frame = useRef<number>();
    const last = useRef(performance.now());
    const init = useRef(performance.now());

    const animate = useCallback(() => {
        if (typeof performance === "undefined" || typeof window === "undefined") {
            return;
        }

        const now = performance.now();
        const time = (now - init.current) / 1000;
        const delta = (now - last.current) / 1000;
        callback({ time, delta });
        last.current = now;
        frame.current = requestAnimationFrame(animate);
    }, [callback]);

    useEffect(() => {
        frame.current = requestAnimationFrame(animate);
        return () => cancelAnimationFrame(frame.current || -1);
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [...deps, animate]);
};

export default useAnimationFrame;
