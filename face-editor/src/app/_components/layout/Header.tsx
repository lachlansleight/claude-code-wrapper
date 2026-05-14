"use client";

import { useState, useEffect, useRef } from "react";
import Link from "next/link";
import { usePathname } from "next/navigation";

import { FaBars, FaSync } from "react-icons/fa";

const linkClass = "text-neutral-500 m-0";
const activeLinkClass = "text-white";

const Header = () => {
    const pathname = usePathname();
    const [width, setWidth] = useState(0);

    const headerRef = useRef<HTMLElement>(null);

    const [menuOpen, setMenuOpen] = useState(false);
    useEffect(() => {
        //make sure dropdown menu is open when window size is greater than 768 (tailwind md breakpoint)
        //this could introduce bugs if we change this breakpoint...
        const handleResize = () => {
            if (headerRef.current) setWidth(headerRef.current.offsetWidth);
        };
        handleResize();
        window.addEventListener("resize", handleResize);
        return () => window.removeEventListener("resize", handleResize);
    }, [headerRef]);

    useEffect(() => {
        setMenuOpen(width > 768);
    }, [width]);

    //close the menu if the user taps anywhere outside of it
    useEffect(() => {
        const handleClick = (e: MouseEvent) => {
            if (window.innerWidth > 768) return;

            if (e.target instanceof Node) {
                if (!headerRef.current?.contains(e.target)) {
                    setMenuOpen(false);
                }
            }
        };

        if (headerRef.current) setWidth(headerRef.current.offsetWidth);

        window.addEventListener("click", handleClick);
        return () => window.removeEventListener("click", handleClick);
    }, [headerRef]);

    return (
        <header
            ref={headerRef}
            className="flex flex-wrap flex-row justify-between items-center h-header md:space-x-4 bg-[#0a0a0a] py-2 px-6 relative"
        >
            <Link href="/" className="flex flex-row items-center">
                <FaSync className="h-6 grid place-items-center" />
                <span className="ml-3 text-xl font-bold text-neutral-400">Web Template</span>
            </Link>
            <button
                aria-expanded={menuOpen}
                className="grid place-items-center md:hidden w-8 h-8 text-neutral-400 border-0 p-0"
                onClick={() => setMenuOpen(!menuOpen)}
            >
                <FaBars className="text-2xl" />
            </button>
            <nav
                className={`${
                    menuOpen ? "flex" : "hidden"
                } absolute md:relative top-12 left-0 md:top-0 z-20 flex-col md:flex-row items-center md:space-x-6 font-semibold w-full md:w-auto bg-neutral-700 md:bg-transparent p-4 md:p-0 shadow-lg md:shadow-none `}
            >
                <Link
                    href="/"
                    className={`${linkClass} ${pathname === "/" ? activeLinkClass : null}`}
                >
                    Home
                </Link>
                <Link
                    href="/other-page"
                    className={`${linkClass} ${
                        pathname.includes("other-page") ? activeLinkClass : null
                    }`}
                >
                    Other Page
                </Link>
            </nav>
        </header>
    );
};

export default Header;
