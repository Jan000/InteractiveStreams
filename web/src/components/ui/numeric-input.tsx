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
  const stepDecimals = React.useMemo(() => {
    if (!step || step <= 0) return undefined;
    const stepStr = String(step);
    return stepStr.includes(".") ? stepStr.split(".")[1].length : 0;
  }, [step]);

  const format = React.useCallback((n: number) => {
    if (integer) return String(Math.round(n));
    if (!Number.isFinite(n)) return "";

    let s: string;
    if (stepDecimals !== undefined) {
      s = n.toFixed(stepDecimals);
    } else {
      s = n.toFixed(10);
    }

    // Only strip trailing zeros after a decimal point — never from integers
    // (the old regex /\.?0+$/ turned "500" → "5" and "0" → "")
    if (s.includes(".")) {
      s = s.replace(/0+$/, "").replace(/\.$/, "");
    }
    return s;
  }, [integer, stepDecimals]);

  const [text, setText] = React.useState(() => format(value));
  const [isEditing, setIsEditing] = React.useState(false);

  // Track the last value that was committed so we can detect external changes
  // (e.g. server poll refresh) vs. the user typing.
  const lastCommitted = React.useRef(value);

  React.useEffect(() => {
    if (!isEditing && value !== lastCommitted.current) {
      lastCommitted.current = value;
      setText(format(value));
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [format, isEditing, value]);

  const parse = (raw: string): number => {
    const normalized = raw.trim().replace(/,/g, ".");
    return integer ? parseInt(normalized, 10) : parseFloat(normalized);
  };

  const clampAndRound = (n: number): number => {
    let v = n;
    if (min !== undefined && isFinite(min)) v = Math.max(min, v);
    if (max !== undefined && isFinite(max)) v = Math.min(max, v);
    if (integer) return Math.round(v);
    return Number(v.toFixed(Math.max(stepDecimals ?? 6, 6)));
  };

  const handleChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const raw = e.target.value;
    setText(raw);
  };

  const commitValue = React.useCallback((raw: string) => {
    let n = parse(raw);
    if (!isFinite(n) || isNaN(n)) {
      n = lastCommitted.current;
    } else {
      n = clampAndRound(n);
    }

    lastCommitted.current = n;
    setText(format(n));
    onChange(n);
  }, [format, onChange]);

  const handleBlur = (e: React.FocusEvent<HTMLInputElement>) => {
    setIsEditing(false);
    commitValue(text);
    onBlur?.(e);
  };

  const handleKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    props.onKeyDown?.(e);
    if (e.defaultPrevented) return;

    if (e.key === "Enter") {
      commitValue(text);
      e.currentTarget.blur();
    } else if (e.key === "Escape") {
      setText(format(lastCommitted.current));
      e.currentTarget.blur();
    }
  };

  return (
    <Input
      {...props}
      type="text"
      inputMode={integer ? "numeric" : "decimal"}
      // Remove native spin buttons (they're useless without type="number")
      className={cn("[appearance:textfield] [&::-webkit-inner-spin-button]:appearance-none [&::-webkit-outer-spin-button]:appearance-none", className)}
      value={text}
      onFocus={(e) => {
        setIsEditing(true);
        props.onFocus?.(e);
      }}
      onChange={handleChange}
      onBlur={handleBlur}
      onKeyDown={handleKeyDown}
    />
  );
}
