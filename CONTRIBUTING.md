# Contributing

Thanks for helping improve Water Level. Contributions can include bug reports, hardware compatibility notes, documentation updates, or code changes.

## Before you start

- Check the existing issues and pull requests for related work.
- For a substantial change, open an issue first to discuss the approach and its hardware impact.
- Do not include Wi-Fi passwords, private broker addresses, tokens, or other credentials in issues or commits. Keep `arduino_secrets.h` local; it is intentionally ignored by Git.

## Making a change

1. Fork the repository and create a focused branch from the default branch.
2. Make the smallest coherent change that addresses the issue.
3. Keep hardware pin assignments, electrical assumptions, and configuration requirements clear in the documentation.
4. Build the sketch in the Arduino IDE for the Arduino Uno WiFi Rev2. If you cannot test on hardware, say so and describe what you did verify.
5. Review your diff for accidental credentials, generated files, and unrelated edits.
6. Open a pull request describing the motivation, behavior changes, build or hardware checks, and any wiring or configuration changes.

## Code and documentation guidance

- Follow the existing Arduino/C++ style and keep changes readable on the target device.
- Avoid adding dependencies unless they are needed; document any new library and setup steps.
- Preserve safe behavior for the solenoid, buzzer, and sensor failure conditions. Explain changes to actuator behavior clearly.
- Include reproducible steps and relevant board/library details when reporting a defect.
- There is currently no automated test suite. At minimum, compile the sketch with the documented board and libraries; include hardware results when available.

## Pull request checklist

- [ ] The change is focused and its purpose is explained.
- [ ] The sketch compiles for the Arduino Uno WiFi Rev2, or any limitation is stated.
- [ ] Hardware and configuration changes are documented.
- [ ] No credentials or unrelated generated files are included.
