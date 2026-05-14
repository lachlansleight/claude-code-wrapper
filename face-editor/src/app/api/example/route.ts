import Server from "next/server";

export const GET = async () => {
    return Server.NextResponse.json({ time: new Date().toString() });
};

export const POST = async (req: Request) => {
    const body = await req.json();

    return Server.NextResponse.json({ time: new Date().toString(), inputData: body });
};
