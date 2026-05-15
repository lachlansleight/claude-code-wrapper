const Panel = ({
  children,
  className = "",
  disabled = false,
}: {
  children: React.ReactNode;
  className?: string;
  disabled?: boolean;
}): JSX.Element => {
  return (
    <div
      className={
        disabled
          ? `pointer-events-none my-1.5 border border-face-border px-3 py-2 opacity-50 ${className}`
          : `my-1.5 border border-face-border px-3 py-2 ${className}`
      }
    >
      {children}
    </div>
  );
};

export default Panel;
