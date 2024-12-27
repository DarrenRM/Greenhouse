# Greenhouse

A Windows utility for managing and restoring window positions across multiple monitors.

## Features

- Save and restore window positions across multiple monitors
- Visual highlighting of saved windows
- Support for different DPI scales
- Automatic detection of monitor changes
- Persistent storage of window positions
- Inactive window tracking

## Requirements

- Windows 10/11
- Python 3.x
- Required packages (see requirements.txt)

## Installation

1. Clone the repository
2. Install required packages: `pip install -r requirements.txt`
3. Run `python app/greenhouse.py`

## Usage

1. Select windows from the list to save their positions
2. Use "Restore All" to move windows back to their saved positions
3. Windows that are not currently running will appear faded in the list
4. When a saved application is launched, you'll be prompted to restore its position 