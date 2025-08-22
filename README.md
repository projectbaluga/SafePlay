<p align="center">
  <img src="assets/SafePlay.png" alt="SafePlay logo"/>
</p>

# SafePlay – Anti-Cheat & Fair-Play
**Fair Gaming, Every Time.**

[![Built with SafePlay](https://img.shields.io/badge/Built%20with-SafePlay-007BFF?style=for-the-badge)](#)
[![Fair Gaming Certified](https://img.shields.io/badge/Fair%20Gaming-Certified-brightgreen?style=for-the-badge)](#)

## Overview
SafePlay is a universal anti-cheat and fair-play system for PC games. The lightweight DLL monitors running processes, validates client integrity, and blocks known cheat tools to keep gameplay fair across titles.

**Target Users:** Game developers, publishers, and server administrators who want to ensure integrity and fairness.

## Features
- Multi-vector detection (process names, window titles, loaded modules, memory signatures)
- Configurable banned lists
- Secure client integrity validation
- Customizable alerts and blocking

## Architecture
SafePlay is a Windows DLL that scans active processes periodically and blocks known cheat tools.

**Planned Enhancements**
- Launcher integration
- Cloud updates
- Configuration tools

## Getting Started
1. Clone the repository:
   ```powershell
   git clone https://github.com/projectbaluga/SafePlay.git
   cd SafePlay
   ```
2. Open `SafePlay.sln` in Visual Studio 2022.
3. Build the project for your target configuration.
4. Deploy `SafePlay.dll` beside your game executable or inject it as needed.

## Roadmap
- Cross-game compatibility
- External configuration file support
- Logging/audit trail
- Cloud-based signature updates
- Localization and user-friendly config tools
- CI/CD pipelines and automated tests

## License
This project is licensed under the [MIT License](LICENSE).
