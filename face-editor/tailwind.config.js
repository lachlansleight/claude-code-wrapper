const colors = require("tailwindcss/colors");

/** @type {import('tailwindcss').Config} */
module.exports = {
    content: ["./src/**/*.{js,ts,jsx,tsx,mdx}"],
    theme: {
        extend: {
            colors: {
                primary: colors.blue,
                secondary: colors.orange,
                neutral: colors.zinc,
            },
            height: {
                header: "3rem",
                footer: "3.5rem",
            },
            width: {
                label: "8rem",
                control: "calc(100% - 8rem)",
            },
            padding: {
                label: "8rem",
            },
            margin: {
                label: "8rem",
            },
            minHeight: theme => ({
                main: `calc(100vh - ${theme("height.header")} - ${theme("height.footer")})`,
                inner: `calc(100vh - ${theme("height.header")} - ${theme(
                    "height.footer"
                )} - 4.5rem)`,
            }),
        },
    },
    plugins: [],
};
