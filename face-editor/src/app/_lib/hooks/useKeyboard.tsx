import { DependencyList, useEffect } from "react";

const useKeyboard = (callback: (e: KeyboardEvent) => void, dependencies: DependencyList): void => {
    useEffect(() => {
        const handleKeyDown = (event: KeyboardEvent) => {
            callback(event);
        };
        document.addEventListener("keydown", handleKeyDown);
        return () => {
            document.removeEventListener("keydown", handleKeyDown);
        };
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [...dependencies, callback]);
};

export default useKeyboard;
