import Header from "_components/layout/Header";
import "./globals.css";
import { Inter } from "next/font/google";
import Footer from "_components/layout/Footer";

const inter = Inter({ subsets: ["latin"] });

export const metadata = {
    title: "Next14 Template",
    description: "A template website with next.js 14, tailwind and typescript",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
    return (
        <html lang="en">
            <body className={`bg-black text-white ${inter.className}`}>
                <Header />
                <main className="pt-4 pb-12 px-4 min-h-main bg-background text-neutral-200 relative overflow-x-hidden">
                    <div className="container mx-auto">{children}</div>
                </main>
                <Footer />
            </body>
        </html>
    );
}
