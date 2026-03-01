"use client";

import * as React from "react";
import { Input } from "@/components/ui/input";
import { cn } from "@/lib/utils";

export interface NumericInputProps
  extends Omit<
    React.ComponentPropsWithoutRef<typeof Input>,
    "value" | "onChange" | "type"
  > {
  /** Controlled numeric value. */
  value: number;
  /** Called with the new number whenever the field commits a valid value. */
  onChange: (n: number) => void;
  /** Minimum allowed value — enforced on blur, never during typing. */
  min?: number;
  /** Maximum allowed value — enforced on blur, never during typing. */
  max?: number;
  /**
   * Increment step. Determines decimal precision when snapping on blur.
   * E.g. step={0.1} → value is rounded to 1 decimal place.
   */
  step?: number;
  /**
   * When true, only whole numbers are accepted.
   * Decimals are stripped on blur.
   */
  integer?: boolean;
}

/**
 * A controlled numeric input that lets users type freely — including partial
 * entries like "1.", "0,5", or just "1" before adding more digits — without
 * the field resetting on every keystroke.
 *
 * Key behaviours:
 * - Raw text is stored internally; clamping/rounding happens only on `blur`.
 * - Both `.` and `,` are accepted as decimal separators.
 * - Uses `inputMode="decimal"` so mobile keyboards show a numeric pad.
 * - Does NOT use `type="number"`, so the browser never swallows partial input
 *   due to `min`/`max` constraint validation.
 */
export function NumericInput({
  value,
  onChange,
  min,
  max,
  step,
  integer = false,
  className,
  onBlur,
  ...props
}: NumericInputProps) {
  const format = (n: number) =>
    integer ? String(Math.round(n)) : String(n);

  const [text, setText] = React.useState(() => format(value));

  // Track the last value that was committed so we can detect external changes
  // (e.g. server poll refresh) vs. the user typing.
  const lastCommitted = React.useRef(value);

  React.useEffect(() => {
    if (value !== lastCommitted.current) {
      lastCommitted.current = value;
      setText(format(value));
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [value]);

  const parse = (raw: string): number => {
    const normalized = raw.trim().replace(/,/g, ".");
    return integer ? parseInt(normalized, 10) : parseFloat(normalized);
  };

  const clampAndRound = (n: number): number => {
    let v = n;
    if (min !== undefined && isFinite(min)) v = Math.max(min, v);
    if (max !== undefined && isFinite(max)) v = Math.min(max, v);
    if (integer) return Math.round(v);
    if (step && step > 0) {
      const decimals = String(step).includes(".")
        ? String(step).split(".")[1].length
        : 0;
      v = Math.round(v / step) * step;
      v = parseFloat(v.toFixed(decimals));
    }
    return v;
  };

  const handleChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const raw = e.target.value;
    setText(raw);
    // Propagate valid intermediate values so the parent state stays
    // roughly in sync while the user is typing (no clamp during editing).
    const n = parse(raw);
    if (isFinite(n) && !isNaN(n)) {
      onChange(n);
    }
  };

  const handleBlur = (e: React.FocusEvent<HTMLInputElement>) => {
    let n = parse(text);
    if (!isFinite(n) || isNaN(n)) {
      // Restore last committed value if input is unparseable
      n = lastCommitted.current;
    } else {
      n = clampAndRound(n);
    }
    lastCommitted.current = n;
    setText(format(n));
    onChange(n);
    onBlur?.(e);
  };

  return (
    <Input
      {...props}
      type="text"
      inputMode={integer ? "numeric" : "decimal"}
      // Remove native spin buttons (they're useless without type="number")
      className={cn("[appearance:textfield] [&::-webkit-inner-spin-button]:appearance-none [&::-webkit-outer-spin-button]:appearance-none", className)}
      value={text}
      onChange={handleChange}
      onBlur={handleBlur}
    />
  );
}
