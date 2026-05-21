import { describe, it, expect, beforeEach, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import { ErrorBoundary } from "../ErrorBoundary";

// React logs errors caught by an error boundary. Silence the noise so
// the test output stays readable.
beforeEach(() => {
  vi.spyOn(console, "error").mockImplementation(() => {});
});

function Boom(): JSX.Element {
  throw new Error("kaboom");
}

describe("<ErrorBoundary />", () => {
  it("renders children when no error is thrown", () => {
    render(
      <ErrorBoundary>
        <div>healthy</div>
      </ErrorBoundary>,
    );
    expect(screen.getByText("healthy")).toBeInTheDocument();
  });

  it("renders a fallback UI with the error message instead of white-screening", () => {
    render(
      <ErrorBoundary>
        <Boom />
      </ErrorBoundary>,
    );
    // The fallback must surface *something* the user can read so we never
    // ship another silent white-screen bug. Assert on the message text and
    // the presence of a reload affordance.
    expect(screen.getByText(/kaboom/)).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /reload|重新加载/i })).toBeInTheDocument();
  });
});
