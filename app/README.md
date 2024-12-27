# Greenhouse Window Manager

A simple utility to save and restore window positions across multiple monitors.

## Features

- List all visible windows
- Save positions of selected windows
- Restore windows to their saved positions
- Clean, modern interface
- Multiple window selection support

## Requirements

- Python 3.6+
- Windows OS

## Installation

1. Clone this repository
2. Install dependencies:
```bash
pip install -r requirements.txt
```

## Usage

1. Run the application:
```bash
python app/greenhouse.py
```

2. The main window will show a list of all currently visible windows
3. Select one or more windows from the list
4. Click "Save Selected" to store their current positions
5. Use "Restore All" to move windows back to their saved positions
6. "Refresh Windows" updates the list of visible windows

## Notes

- Windows must be visible (not minimized) to be tracked
- Window positions are stored in memory and will be cleared when the application is closed
- The application needs to be running to restore window positions 