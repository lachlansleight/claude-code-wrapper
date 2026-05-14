import packageJson from "../../../../package.json";

const Footer = (): JSX.Element => {
  return (
    <footer className="px-4 flex items-center h-footer bg-neutral-900 text-neutral-400">
      <div className="container mx-auto flex justify-between">
        <p className="m-0">Made with love</p>
        <p className="m-0">v{packageJson.version}</p>
      </div>
    </footer>
  );
};

export default Footer;
