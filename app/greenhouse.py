"""
Greenhouse - Window Position Manager
A utility for saving and restoring window positions across multiple monitors.
"""

import tkinter as tk
from tkinter import ttk, messagebox
import win32gui
import win32api
import win32process
import win32con
import win32ui
import win32gui_struct
import ctypes
import psutil
import threading
import time
import signal
import sys
import json
import os
import winreg
import subprocess
import logging
from datetime import datetime
from PIL import Image, ImageDraw  # For tray icon

def setup_logging():
    """Setup logging with a new file for each run."""
    # Create logs directory if it doesn't exist
    log_dir = os.path.join(os.path.dirname(__file__), "logs")
    os.makedirs(log_dir, exist_ok=True)
    
    # Create log filename with timestamp
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = os.path.join(log_dir, f"greenhouse_{timestamp}.log")
    
    # Configure logging
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(levelname)s - %(message)s',
        handlers=[
            logging.FileHandler(log_file),
            logging.StreamHandler()  # Also log to console
        ]
    )
    
    logging.info("Starting Greenhouse Window Manager")
    return log_file

def wndproc(hwnd, msg, wparam, lparam):
    """Window procedure for overlay windows."""
    logging.debug(f"wndproc called for hwnd: {hwnd}, msg: {msg}")
    if msg == win32con.WM_DESTROY:
        return 0
    elif msg == win32con.WM_PAINT:
        logging.debug(f"WM_PAINT received for hwnd: {hwnd}")
        ps = win32gui.BeginPaint(hwnd)
        try:
            rect = win32gui.GetClientRect(hwnd)
            left, top, right, bottom = rect
            width = right - left
            height = bottom - top

            # Create a solid green brush for testing
            brush = win32gui.CreateSolidBrush(win32api.RGB(0, 255, 0))
            # Fill the entire window with green (for testing)
            win32gui.FillRect(ps[0], rect, brush)
            win32gui.DeleteObject(brush)

        except Exception as e:
            logging.error(f"Error in WM_PAINT: {e}")
        finally:
            win32gui.EndPaint(hwnd, ps[1])
        return 0
    elif msg == win32con.WM_NCHITTEST:
        return win32con.HTTRANSPARENT
    return win32gui.DefWindowProc(hwnd, msg, wparam, lparam)

def create_overlay_window():
    """Create a transparent overlay window."""
    try:
        hInstance = win32api.GetModuleHandle(None)
        class_name = f'GreenHouseOverlay_{id(threading.current_thread())}'
        
        # Define window class
        wndClass = win32gui.WNDCLASS()
        wndClass.lpfnWndProc = wndproc
        wndClass.lpszClassName = class_name
        wndClass.hInstance = hInstance
        wndClass.hCursor = win32gui.LoadCursor(None, win32con.IDC_ARROW)
        wndClass.style = win32con.CS_HREDRAW | win32con.CS_VREDRAW
        wndClass.hbrBackground = None  # Let WM_PAINT handle all drawing
        
        try:
            win32gui.RegisterClass(wndClass)
        except win32gui.error as e:
            if e.winerror != 1410:  # Ignore "Class already exists"
                raise
        
        # Create window with modified styles
        hwnd = win32gui.CreateWindowEx(
            win32con.WS_EX_LAYERED | win32con.WS_EX_TOPMOST | win32con.WS_EX_TOOLWINDOW,
            class_name,
            None,
            win32con.WS_POPUP | win32con.WS_VISIBLE,
            0, 0, 100, 100,  # Initial size
            None, None,
            hInstance,
            None
        )
        
        if not hwnd:
            raise Exception("Failed to create window")
        
        # Set the window to be semi-transparent (128 = 50% opacity)
        win32gui.SetLayeredWindowAttributes(hwnd, 0, 64, win32con.LWA_ALPHA)
        
        logging.info(f"Created overlay window: {hwnd}")
        return hwnd
        
    except Exception as e:
        logging.error(f"Failed to create overlay window: {e}")
        return None

class Settings:
    """Handles application settings and persistence."""
    
    def __init__(self):
        self.data_file = os.path.expanduser("~/.greenhouse/saved_positions.json")
        os.makedirs(os.path.dirname(self.data_file), exist_ok=True)
        self.data = self.load_data()
    
    def load_data(self):
        """Load all data from file."""
        try:
            with open(self.data_file, 'r') as f:
                return json.load(f)
        except (FileNotFoundError, json.JSONDecodeError):
            return {
                "settings": {
                    "start_with_windows": False
                },
                "windows": {}
            }
    
    def save_data(self):
        """Save all data to file."""
        with open(self.data_file, 'w') as f:
            json.dump(self.data, f, indent=2)
    
    def load_window_positions(self):
        """Load saved window positions."""
        return self.data.get("windows", {})
    
    def save_window_positions(self, positions):
        """Save window positions to file.
        
        Args:
            positions: Dictionary of window information and positions
        """
        self.data["windows"] = positions
        self.save_data()
    
    def set_startup(self, enable):
        """Configure application to run at startup."""
        key_path = r"Software\Microsoft\Windows\CurrentVersion\Run"
        app_path = os.path.abspath(sys.argv[0])
        
        try:
            key = winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path, 0, 
                               winreg.KEY_WRITE | winreg.KEY_READ)
            if enable:
                winreg.SetValueEx(key, "Greenhouse", 0, winreg.REG_SZ, f'pythonw "{app_path}"')
            else:
                try:
                    winreg.DeleteValue(key, "Greenhouse")
                except FileNotFoundError:
                    pass
            self.data["settings"]["start_with_windows"] = enable
            self.save_data()
        except Exception as e:
            messagebox.showerror("Error", f"Failed to update startup settings: {str(e)}")
        finally:
            try:
                key.Close()
            except:
                pass

    @property
    def settings(self):
        """Get settings dictionary."""
        return self.data["settings"]

class CustomListbox(tk.Frame):
    """Custom listbox with icons and two-line items."""
    
    def __init__(self, master, **kwargs):
        super().__init__(master)
        self.canvas = tk.Canvas(self, bg='white')
        self.scrollbar = ttk.Scrollbar(self, orient=tk.VERTICAL, command=self.canvas.yview)
        
        # Frame to contain the items
        self.items_frame = ttk.Frame(self.canvas, style='White.TFrame')
        self.canvas.configure(yscrollcommand=self.scrollbar.set)
        
        # Layout
        self.canvas.grid(row=0, column=0, sticky="nsew")
        self.scrollbar.grid(row=0, column=1, sticky="ns")
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(0, weight=1)
        
        # Create canvas window
        self.canvas_window = self.canvas.create_window((0, 0), window=self.items_frame, anchor="nw")
        
        # Bind events
        self.canvas.bind('<Configure>', self._on_canvas_configure)
        self.items_frame.bind('<Configure>', self._on_frame_configure)
        self.selected_indices = set()
        self.items = []
        self.on_selection_change = None  # Callback for selection changes
    
    def _on_canvas_configure(self, event):
        self.canvas.itemconfig(self.canvas_window, width=event.width)
    
    def _on_frame_configure(self, event):
        self.canvas.configure(scrollregion=self.canvas.bbox("all"))
    
    def delete(self, start, end=None):
        """Clear items from the listbox."""
        for widget in self.items_frame.winfo_children():
            widget.destroy()
        self.items.clear()
        self.selected_indices.clear()
    
    def insert(self, index, icon, process_name, title, hwnd):
        """Insert a new item with icon and two lines of text."""
        # Container frame for the entire item
        container_frame = ttk.Frame(self.items_frame, style='White.TFrame')
        container_frame.pack(fill=tk.X, expand=True)
        
        # Main item frame that will be highlighted
        item_frame = ttk.Frame(container_frame, style='White.TFrame')
        item_frame.pack(fill=tk.X, expand=True, padx=2, pady=1)
        
        # Create icon label with fixed width for alignment
        icon_frame = ttk.Frame(item_frame, width=32, height=32, style='White.TFrame')
        icon_frame.pack(side=tk.LEFT, padx=(5, 10))
        icon_frame.pack_propagate(False)  # Maintain fixed width
        
        icon_label = tk.Label(icon_frame, bg='white')  # Use tk.Label instead of ttk.Label for better image support
        icon_label.pack(expand=True, fill=tk.BOTH)
        if icon:
            icon_label.configure(image=icon)
            icon_label.image = icon  # Keep a reference
        
        # Text frame for process name and title
        text_frame = ttk.Frame(item_frame, style='White.TFrame')
        text_frame.pack(side=tk.LEFT, fill=tk.X, expand=True)
        
        # Process name (capitalized) and title
        process_label = ttk.Label(text_frame, text=process_name.capitalize(), 
                                font=('TkDefaultFont', 10, 'bold'),
                                style='White.TLabel')
        process_label.pack(anchor=tk.W, pady=(0, 0))  # Removed top padding
        title_label = ttk.Label(text_frame, text=title, style='White.TLabel')
        title_label.pack(anchor=tk.W)
        
        # Store item info
        item_info = {
            'container': container_frame,
            'frame': item_frame,
            'icon_frame': icon_frame,
            'text_frame': text_frame,
            'process_label': process_label,
            'title_label': title_label,
            'icon_label': icon_label,
            'hwnd': hwnd,
            'index': len(self.items)
        }
        self.items.append(item_info)
        
        # Bind events for all widgets
        for widget in [container_frame, item_frame, icon_frame, icon_label, 
                      process_label, title_label, text_frame]:
            if widget:
                widget.bind('<Button-1>', lambda e, i=item_info: self._on_item_click(i))
    
    def _on_item_click(self, item_info):
        """Handle item selection."""
        was_selected = item_info['index'] in self.selected_indices
        
        if was_selected:
            # Deselect the item
            self.selected_indices.remove(item_info['index'])
            for widget in [item_info['container'], item_info['frame'], item_info['icon_frame'], item_info['text_frame']]:
                widget.configure(style='White.TFrame')
            for widget in [item_info['process_label'], item_info['title_label']]:
                widget.configure(style='White.TLabel')
            if item_info['icon_label']:
                item_info['icon_label'].configure(bg='white')  # Use bg for tk.Label
        else:
            # Select the item
            self.selected_indices.add(item_info['index'])
            for widget in [item_info['container'], item_info['frame'], item_info['icon_frame'], item_info['text_frame']]:
                widget.configure(style='Selected.TFrame')
            for widget in [item_info['process_label'], item_info['title_label']]:
                widget.configure(style='Selected.TLabel')
            if item_info['icon_label']:
                item_info['icon_label'].configure(bg='#2E8B57')  # Match Selected.TFrame background
        
        # Notify about selection change if callback is set
        if self.on_selection_change:
            self.on_selection_change(item_info['hwnd'], not was_selected)
    
    def curselection(self):
        """Return the selected indices."""
        return tuple(sorted(self.selected_indices))
    
    def _on_focus_out(self, event):
        """Remove highlights when focus is lost."""
        if self.highlight_callback:
            # Only remove window border highlights, keep selection
            for item in self.items:
                if item['index'] in self.selected_indices:
                    self.highlight_callback(item['hwnd'], False)
    
    def update_icon(self, index, icon):
        """Update the icon for an item after it's loaded."""
        if index < len(self.items):
            item = self.items[index]
            if 'icon_label' in item and item['icon_label']:
                logging.info(f"Updating icon for item {index}")
                item['icon_label'].configure(image=icon)
                item['icon_label'].image = icon  # Keep a reference to prevent garbage collection
                self.update_idletasks()  # Force a redraw

class WindowManager:
    """Handles window management operations using the Windows API."""
    
    def __init__(self):
        self.settings = Settings()
        self.saved_window_positions = {}
        self.current_windows = []
        self.previous_monitor_count = self.get_monitor_count()
        self.monitor_check_active = True
        self.monitor_thread = None
        self.icon_cache = {}  # Cache for window icons
        self.overlay_windows = {}  # Store overlay window handles
        
        # Load saved positions
        self.load_saved_positions()
        logging.info(f"Loaded {len(self.saved_window_positions)} saved window positions")
        
        # Restore windows immediately if auto-starting
        if self.settings.settings["start_with_windows"]:
            logging.info("Auto-start enabled, restoring windows")
            for hwnd_str, data in self.saved_window_positions.items():
                try:
                    hwnd = self.find_matching_window(data["info"])
                    if hwnd:
                        self.restore_window_position(hwnd)
                except Exception as e:
                    logging.error(f"Failed to restore window {hwnd_str}: {str(e)}")
    
    def get_window_info(self, hwnd):
        """Get detailed window information for persistence."""
        try:
            _, pid = win32process.GetWindowThreadProcessId(hwnd)
            process = psutil.Process(pid)
            return {
                "title": win32gui.GetWindowText(hwnd),
                "process_name": process.name(),
                "process_path": process.exe(),
                "class_name": win32gui.GetClassName(hwnd)
            }
        except:
            return None
    
    def find_matching_window(self, saved_info):
        """Find a window matching saved information."""
        def callback(hwnd, matches):
            if not self.is_window_interesting(hwnd):
                return
            
            current_info = self.get_window_info(hwnd)
            if not current_info:
                return
            
            # Match based on multiple criteria
            if (current_info["process_path"] == saved_info["process_path"] and
                current_info["class_name"] == saved_info["class_name"] and
                current_info["title"] == saved_info["title"]):
                matches.append(hwnd)
        
        matches = []
        win32gui.EnumWindows(callback, matches)
        return matches[0] if matches else None
    
    def save_positions_to_disk(self):
        """Save current window positions to disk."""
        positions_to_save = {}
        for hwnd, pos in self.saved_window_positions.items():
            window_info = self.get_window_info(hwnd)
            if window_info:
                positions_to_save[str(hwnd)] = {
                    "info": window_info,
                    "position": pos
                }
        self.settings.save_window_positions(positions_to_save)
    
    def load_saved_positions(self):
        """Load saved positions from disk."""
        saved_data = self.settings.load_window_positions()
        self.saved_window_positions.clear()
        
        for hwnd_str, data in saved_data.items():
            hwnd = self.find_matching_window(data["info"])
            if hwnd:
                self.saved_window_positions[hwnd] = data["position"]
    
    def get_monitor_count(self):
        """Get the number of active monitors."""
        return len(win32api.EnumDisplayMonitors())

    def get_process_name(self, hwnd):
        """Get the process name for a window handle."""
        try:
            _, pid = win32process.GetWindowThreadProcessId(hwnd)
            process = psutil.Process(pid)
            return process.name()
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            return "Unknown"

    def start_monitor_check(self, callback):
        """Start background thread to check monitor count."""
        def check_monitors():
            while self.monitor_check_active:
                try:
                    current_count = self.get_monitor_count()
                    
                    # Only act when monitors are reconnected (1 -> 2)
                    if self.previous_monitor_count == 1 and current_count == 2:
                        logging.info(f"Second monitor connected (Count: {current_count})")
                        if self.saved_window_positions:
                            callback()
                    elif self.previous_monitor_count != current_count:
                        logging.info(f"Monitor count changed: {self.previous_monitor_count} -> {current_count}")
                    
                    self.previous_monitor_count = current_count
                    time.sleep(2)
                except Exception as e:
                    logging.error(f"Error in monitor check thread: {str(e)}")
                    break

        self.monitor_thread = threading.Thread(target=check_monitors, daemon=True)
        self.monitor_thread.start()
        logging.info("Monitor check thread started")

    def stop_monitor_check(self):
        """Stop the monitor checking thread."""
        logging.info("Stopping monitor check thread")
        self.monitor_check_active = False
        if self.monitor_thread:
            self.monitor_thread.join(timeout=1.0)
            logging.info("Monitor check thread stopped")

    def is_window_interesting(self, hwnd):
        """Filter for windows we want to track.
        
        Args:
            hwnd: Window handle to check
            
        Returns:
            bool: True if window should be tracked, False otherwise
        """
        # Skip invisible windows
        if not win32gui.IsWindowVisible(hwnd):
            return False
        # Skip minimized windows
        if win32gui.IsIconic(hwnd):
            return False
        # Skip windows without titles
        title = win32gui.GetWindowText(hwnd)
        if not title:
            return False
        return True

    def enum_windows(self):
        """Get list of all trackable windows.
        
        Returns:
            list: List of (hwnd, title, process_name) tuples for all visible windows
        """
        self.current_windows.clear()
        
        def callback(hwnd, extra):
            if self.is_window_interesting(hwnd):
                title = win32gui.GetWindowText(hwnd)
                process_name = self.get_process_name(hwnd)
                self.current_windows.append((hwnd, title, process_name))
        
        win32gui.EnumWindows(callback, None)
        return self.current_windows

    def get_monitor_dpi(self, hwnd=None, point=None):
        """Get the DPI of the monitor containing the window or point.
        
        Args:
            hwnd: Window handle (optional)
            point: (x, y) tuple of screen coordinates (optional)
            
        At least one of hwnd or point must be provided.
        """
        try:
            if point:
                monitor = win32api.MonitorFromPoint(point)
            else:
                monitor = win32api.MonitorFromWindow(hwnd)
            
            # Get DPI for the monitor
            if point:
                # For a point, we need to use GetDpiForMonitor
                dpi_x = win32api.GetDpiForMonitor(monitor, 0)[0]  # MDT_EFFECTIVE_DPI = 0
            else:
                dpi_x = win32gui.GetDpiForWindow(hwnd)
            
            return dpi_x / 96.0  # 96 is the default DPI
        except:
            return 1.0  # Default scale factor if we can't get DPI

    def save_window_position(self, hwnd):
        """Save position and size of a specific window.
        
        Args:
            hwnd: Window handle to save position for
        """
        rect = win32gui.GetWindowRect(hwnd)
        left, top, right, bottom = rect
        width = right - left
        height = bottom - top
        
        # Save DPI scale with the position
        dpi_scale = self.get_monitor_dpi(hwnd)
        
        # Save normalized coordinates (as if DPI was 96)
        position_info = {
            "left": int(left / dpi_scale),
            "top": int(top / dpi_scale),
            "width": int(width / dpi_scale),
            "height": int(height / dpi_scale),
            "dpi_scale": dpi_scale
        }
        
        # Save to in-memory positions
        self.saved_window_positions[hwnd] = position_info
        
        # Get window info
        window_info = self.get_window_info(hwnd)
        if window_info:
            # Load current saved positions
            saved_positions = self.settings.load_window_positions()
            
            # Update with new window info and position
            saved_positions[str(hwnd)] = {
                "info": window_info,
                "position": position_info
            }
            
            # Save back to disk
            self.settings.save_window_positions(saved_positions)
            logging.info(f"Saved window position and info for {hwnd} to disk")

    def restore_window_position(self, hwnd):
        """Restore a window to its saved position.
        
        Args:
            hwnd: Window handle to restore position for
        """
        if hwnd in self.saved_window_positions:
            pos = self.saved_window_positions[hwnd]
            
            # Get current and target monitor info
            current_monitor = win32api.MonitorFromWindow(hwnd)
            target_monitor = win32api.MonitorFromPoint((int(pos["left"]), int(pos["top"])))
            
            if current_monitor != target_monitor:
                # Moving between monitors - do it in two steps
                
                # Step 1: Move to the target monitor first (using current DPI)
                current_dpi = self.get_monitor_dpi(hwnd)
                initial_left = int(pos["left"] * current_dpi)
                initial_top = int(pos["top"] * current_dpi)
                initial_width = int(pos["width"] * current_dpi)
                initial_height = int(pos["height"] * current_dpi)
                win32gui.MoveWindow(hwnd, initial_left, initial_top, initial_width, initial_height, True)
                
                # Let the window settle in its new monitor
                time.sleep(0.1)
                
                # Step 2: Now adjust for the target monitor's DPI
                target_dpi = self.get_monitor_dpi(hwnd)  # Get DPI now that window is on target monitor
                final_left = int(pos["left"] * target_dpi)
                final_top = int(pos["top"] * target_dpi)
                final_width = int(pos["width"] * target_dpi)
                final_height = int(pos["height"] * target_dpi)
                win32gui.MoveWindow(hwnd, final_left, final_top, final_width, final_height, True)
            else:
                # Same monitor - do it in one step
                target_dpi = self.get_monitor_dpi(hwnd)
                left = int(pos["left"] * target_dpi)
                top = int(pos["top"] * target_dpi)
                width = int(pos["width"] * target_dpi)
                height = int(pos["height"] * target_dpi)
                win32gui.MoveWindow(hwnd, left, top, width, height, True)
            
            # Update the saved DPI scale
            pos["dpi_scale"] = target_dpi

            # Commented out for now - this might be causing the windows to be saved AFTER a monitor is turned off and they move
            # self.save_positions_to_disk()

    def get_window_icon(self, hwnd):
        """Get the window's icon as a PhotoImage."""
        # Check cache first
        if hwnd in self.icon_cache:
            return self.icon_cache[hwnd]

        try:
            import win32gui
            import win32ui
            import win32con
            from PIL import Image, ImageTk
            
            # Try to get the process path and extract icon from exe
            _, pid = win32process.GetWindowThreadProcessId(hwnd)
            process = psutil.Process(pid)
            exe_path = process.exe()
            
            # Extract icon from executable
            large_icons, small_icons = win32gui.ExtractIconEx(exe_path, 0)
            if not large_icons:
                return None
                
            icon_handle = large_icons[0]
            
            # Create DC and bitmap
            screen_dc = win32gui.GetDC(0)
            hdc = win32ui.CreateDCFromHandle(screen_dc)
            hdc_mem = hdc.CreateCompatibleDC()
            
            # Create bitmap and select it into DC
            hbmp = win32ui.CreateBitmap()
            hbmp.CreateCompatibleBitmap(hdc, 32, 32)
            previous_bitmap = hdc_mem.SelectObject(hbmp)
            
            # Set white background
            hdc_mem.FillSolidRect((0, 0, 32, 32), win32api.RGB(255, 255, 255))
            
            # Draw icon
            win32gui.DrawIconEx(hdc_mem.GetHandleOutput(), 0, 0, icon_handle, 32, 32, 0, None, win32con.DI_NORMAL)
            
            # Convert to PIL Image
            bmpinfo = hbmp.GetInfo()
            bmpstr = hbmp.GetBitmapBits(True)
            img = Image.frombuffer(
                'RGBA',
                (bmpinfo['bmWidth'], bmpinfo['bmHeight']),
                bmpstr, 'raw', 'BGRA', 0, 1
            )
            
            # Ensure image is in RGBA mode
            if img.mode != 'RGBA':
                img = img.convert('RGBA')
            
            # Clean up
            hdc_mem.SelectObject(previous_bitmap)
            win32gui.DestroyIcon(icon_handle)
            for icon in large_icons[1:]:
                win32gui.DestroyIcon(icon)
            for icon in small_icons:
                win32gui.DestroyIcon(icon)
            hdc_mem.DeleteDC()
            hdc.DeleteDC()
            win32gui.ReleaseDC(0, screen_dc)
            
            # Convert to PhotoImage
            photo = ImageTk.PhotoImage(img)
            self.icon_cache[hwnd] = photo
            return photo
            
        except Exception as e:
            logging.error(f"Failed to get icon for window {hwnd}: {str(e)}")
            return None
    
    def create_overlay_window(self, x=0, y=0):
        """Create a transparent overlay window.
        
        Args:
            x: Initial x coordinate in virtual screen space
            y: Initial y coordinate in virtual screen space
        """
        try:
            # Get the monitor for the target coordinates
            monitor = win32api.MonitorFromPoint((x, y), win32con.MONITOR_DEFAULTTONEAREST)
            monitor_info = win32api.GetMonitorInfo(monitor)
            monitor_rect = monitor_info['Monitor']
            logging.info(f"Creating overlay window at ({x}, {y}) on monitor: {monitor_rect}")

            # Create window class
            hInstance = win32api.GetModuleHandle(None)
            class_name = f'GreenHouseOverlay_{id(self)}_{len(self.overlay_windows)}'
            
            # Create window class
            wndClass = win32gui.WNDCLASS()
            wndClass.lpfnWndProc = wndproc
            wndClass.lpszClassName = class_name
            wndClass.hInstance = hInstance
            wndClass.hCursor = win32gui.LoadCursor(None, win32con.IDC_ARROW)
            wndClass.style = win32con.CS_HREDRAW | win32con.CS_VREDRAW | win32con.CS_GLOBALCLASS
            wndClass.hbrBackground = win32gui.CreateSolidBrush(win32api.RGB(0, 255, 0))
            
            try:
                win32gui.RegisterClass(wndClass)
            except win32gui.error as e:
                if e.winerror != 1410:  # Ignore "Class already exists"
                    raise

            # Create window without thread attachment first
            hwnd = win32gui.CreateWindowEx(
                win32con.WS_EX_LAYERED | win32con.WS_EX_TOPMOST | win32con.WS_EX_TOOLWINDOW | win32con.WS_EX_TRANSPARENT | win32con.WS_EX_NOACTIVATE,
                class_name,
                None,
                win32con.WS_POPUP,  # Create hidden initially
                x, y, 100, 100,  # Initial position and size
                None, None,
                hInstance,
                None
            )
            
            if not hwnd:
                raise Exception("Failed to create window")
            
            # Set the window to be very transparent (32 = ~12.5% opacity)
            win32gui.SetLayeredWindowAttributes(hwnd, 0, 64, win32con.LWA_ALPHA)
            
            # Get the target window for the monitor
            monitor_window = win32gui.WindowFromPoint((x, y))
            if monitor_window:
                # Get thread IDs
                current_thread = win32api.GetCurrentThreadId()
                window_thread, _ = win32process.GetWindowThreadProcessId(monitor_window)
                
                if current_thread != window_thread:
                    try:
                        # Attach to the window's thread
                        attached = win32process.AttachThreadInput(current_thread, window_thread, True)
                        if attached:
                            # Move and show the window while attached
                            win32gui.SetWindowPos(
                                hwnd,
                                win32con.HWND_TOPMOST,
                                x, y,
                                100, 100,
                                win32con.SWP_NOSIZE | win32con.SWP_NOACTIVATE | win32con.SWP_SHOWWINDOW
                            )
                            # Detach from the thread
                            win32process.AttachThreadInput(current_thread, window_thread, False)
                    except Exception as e:
                        logging.warning(f"Thread attachment failed: {e}")
            
            # If thread attachment failed or wasn't needed, still try to show the window
            win32gui.SetWindowPos(
                hwnd,
                win32con.HWND_TOPMOST,
                x, y,
                100, 100,
                win32con.SWP_NOSIZE | win32con.SWP_NOACTIVATE | win32con.SWP_SHOWWINDOW
            )
            
            # Force window to be visible
            win32gui.ShowWindow(hwnd, win32con.SW_SHOWNA)
            win32gui.UpdateWindow(hwnd)
            
            return hwnd
            
        except Exception as e:
            logging.error(f"Failed to create overlay window: {e}")
            return None

    def highlight_window(self, hwnd, highlight=True):
        """Add or remove green highlight from a window."""
        try:
            if not win32gui.IsWindow(hwnd):
                return

            # Get target window position and size
            rect = win32gui.GetWindowRect(hwnd)
            left, top, right, bottom = rect
            width = right - left
            height = bottom - top

            # Get the monitor info for the target window
            monitor = win32api.MonitorFromWindow(hwnd)
            monitor_info = win32api.GetMonitorInfo(monitor)
            monitor_rect = monitor_info['Monitor']
            logging.info(f"Target window {hwnd} on monitor: {monitor_rect}")

            # Create overlay if it doesn't exist yet
            if hwnd not in self.overlay_windows:
                overlay_hwnd = self.create_overlay_window(left, top)  # Create at target position
                if overlay_hwnd:
                    self.overlay_windows[hwnd] = overlay_hwnd
                    logging.info(f"Created overlay {overlay_hwnd} for window {hwnd}")
                else:
                    return

            overlay_hwnd = self.overlay_windows[hwnd]

            # Update position and show/hide
            if highlight:
                # Position and show the overlay
                win32gui.SetWindowPos(
                    overlay_hwnd,
                    win32con.HWND_TOPMOST,
                    left,
                    top,
                    width,
                    height,
                    win32con.SWP_NOACTIVATE | win32con.SWP_SHOWWINDOW
                )

                # Verify overlay is on correct monitor
                overlay_monitor = win32api.MonitorFromWindow(overlay_hwnd)
                if overlay_monitor != monitor:
                    logging.warning("Overlay on wrong monitor, forcing position")
                    win32gui.SetWindowPos(
                        overlay_hwnd,
                        win32con.HWND_TOPMOST,
                        left,
                        top,
                        width,
                        height,
                        win32con.SWP_NOACTIVATE | win32con.SWP_SHOWWINDOW
                    )

                # Force a repaint
                win32gui.InvalidateRect(overlay_hwnd, None, True)
                win32gui.UpdateWindow(overlay_hwnd)
            else:
                # Just hide the overlay
                win32gui.ShowWindow(overlay_hwnd, win32con.SW_HIDE)

        except Exception as e:
            logging.error(f"Failed to highlight window {hwnd}: {str(e)}")
            if hwnd in self.overlay_windows:
                try:
                    win32gui.DestroyWindow(self.overlay_windows[hwnd])
                except:
                    pass
                del self.overlay_windows[hwnd]

    def cleanup_overlays(self):
        """Remove all overlay highlights."""
        for hwnd, overlay_hwnd in list(self.overlay_windows.items()):
            try:
                win32gui.DestroyWindow(overlay_hwnd)
            except:
                pass
        self.overlay_windows.clear()

class SettingsDialog:
    """Settings dialog window."""
    
    def __init__(self, parent, settings):
        self.dialog = tk.Toplevel(parent)
        self.dialog.title("Settings")
        self.dialog.geometry("300x200")
        self.dialog.transient(parent)
        self.dialog.grab_set()
        
        self.settings = settings
        
        # Main frame with padding
        frame = ttk.Frame(self.dialog, padding="10")
        frame.pack(fill=tk.BOTH, expand=True)
        
        # Startup option
        self.startup_var = tk.BooleanVar(value=settings.settings["start_with_windows"])
        ttk.Checkbutton(frame, text="Start with Windows", 
                       variable=self.startup_var).pack(pady=5)
        
        # Separator
        ttk.Separator(frame, orient='horizontal').pack(fill='x', pady=10)
        
        # File viewing section
        file_frame = ttk.LabelFrame(frame, text="View Data File", padding="5")
        file_frame.pack(fill='x', pady=5)
        
        ttk.Button(file_frame, text="View Saved Positions", 
                  command=lambda: self.view_file(self.settings.data_file)).pack(fill='x', pady=2)
        
        # File path
        path_frame = ttk.Frame(file_frame)
        path_frame.pack(fill='x', pady=5)
        ttk.Label(path_frame, text=f"Location: {os.path.dirname(self.settings.data_file)}", 
                 wraplength=250).pack()
        
        # Buttons
        btn_frame = ttk.Frame(frame)
        btn_frame.pack(side=tk.BOTTOM, pady=10)
        ttk.Button(btn_frame, text="Save", command=self.save).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="Cancel", command=self.dialog.destroy).pack(side=tk.LEFT)
    
    def view_file(self, file_path):
        """Open a file in the default text editor."""
        try:
            if os.path.exists(file_path):
                if sys.platform == 'win32':
                    os.startfile(file_path)
                else:
                    subprocess.run(['xdg-open', file_path])
            else:
                messagebox.showinfo("File Not Found", 
                                  "The file hasn't been created yet. It will be created when you save settings or window positions.")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to open file: {str(e)}")
    
    def save(self):
        """Save settings and close dialog."""
        self.settings.set_startup(self.startup_var.get())
        self.dialog.destroy()

class WindowManagerGUI:
    """GUI interface for the window manager."""
    
    def __init__(self, root):
        self.root = root
        self.root.title("Greenhouse - Window Position Manager")
        self.root.geometry("720x560")  # 600 * 1.2 = 720 width, 400 * 1.4 = 560 height
        
        self.window_manager = WindowManager()
        self.is_focused = False  # Add focus tracking
        self.highlighted_windows = set()  # Track which windows are currently highlighted
        self.last_selection_time = {}  # Track last selection time for each window
        self.debounce_delay = 200  # Milliseconds to wait before processing selection
        self.pending_highlights = {}  # Track pending highlight operations
        
        self.setup_gui()
        self.setup_tray()
        
        # Start monitor detection
        self.window_manager.start_monitor_check(self.on_monitors_reconnected)
        
        # Handle window close button to minimize to tray instead
        self.root.protocol("WM_DELETE_WINDOW", self.minimize_to_tray)
        signal.signal(signal.SIGINT, self.signal_handler)
        
        # Bind focus events
        self.root.bind("<FocusIn>", self._on_focus_in)
        self.root.bind("<FocusOut>", self._on_focus_out)
    
    def setup_gui(self):
        """Create and configure the GUI elements."""
        # Create main frame with padding
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # Create styles
        style = ttk.Style()
        style.configure('White.TFrame', background='white')
        style.configure('White.TLabel', background='white')
        style.configure('Hover.TFrame', background='#F0F0F0')  # Light gray for hover
        style.configure('Hover.TLabel', background='#F0F0F0')
        style.configure('Selected.TFrame', background='#2E8B57')  # Sea Green
        style.configure('Selected.TLabel', background='#2E8B57', foreground='white')
        style.configure('Inactive.TFrame', background='#93e9be')  # Very light green
        style.configure('Inactive.TLabel', background='#93e9be', foreground='#757575')  # Gray text
        
        # Add instruction label
        instruction_label = ttk.Label(main_frame, text="Select windows to save their position:", 
                                    font=('TkDefaultFont', 10))
        instruction_label.grid(row=0, column=0, columnspan=2, sticky=tk.W, pady=(0, 5))
        
        # Custom listbox with icons
        self.window_listbox = CustomListbox(main_frame)
        self.window_listbox.grid(row=1, column=0, columnspan=2, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # Set selection change callback
        self.window_listbox.on_selection_change = self.on_window_selection_change
        
        # Button frame
        btn_frame = ttk.Frame(main_frame)
        btn_frame.grid(row=2, column=0, columnspan=2, pady=5)
        
        # Action buttons
        ttk.Button(btn_frame, text="Refresh Windows", 
                  command=self.refresh_windows).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="Restore All", 
                  command=self.restore_all).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="Settings", 
                  command=self.show_settings).pack(side=tk.LEFT, padx=5)
        
        # Configure grid weights for proper resizing
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main_frame.columnconfigure(0, weight=1)
        main_frame.columnconfigure(1, weight=1)
        main_frame.rowconfigure(1, weight=1)  # Changed from 0 to 1 since we added a label
        
        # Initial population of window list
        self.refresh_windows()
        
        # Start window state monitoring
        self.start_window_monitor()

    def start_window_monitor(self):
        """Start periodic window state checking."""
        self.check_window_states()
        # Check every 2 seconds
        self.root.after(2000, self.start_window_monitor)

    def check_window_states(self):
        """Check if any windows have been closed or launched and update UI."""
        current_windows = {}
        changes = False
        
        # Get current window list with full info
        for hwnd, title, process_name in self.window_manager.enum_windows():
            current_windows[hwnd] = {
                'title': title,
                'process_name': process_name
            }
        
        # Load saved positions once and make a copy for safe iteration
        saved_positions = dict(self.window_manager.settings.load_window_positions())
        
        # Set flag to prevent removing saved positions during window close handling
        self._handling_window_close = True
        try:
            # First check if any saved but inactive windows have become active
            for hwnd_str, saved_data in list(saved_positions.items()):
                hwnd = int(hwnd_str)
                saved_info = saved_data.get("info", {})
                
                # Look for a matching window in current_windows
                for current_hwnd, current_info in current_windows.items():
                    if (current_info['process_name'].lower() == saved_info.get('process_name', '').lower() and
                        current_info['title'] == saved_info.get('title', '')):
                        # Found a match - check if it was previously inactive
                        for item in self.window_listbox.items:
                            if (item.get('hwnd') == hwnd and 
                                item.get('inactive', False)):
                                changes = True
                                # Update item with new hwnd and mark as active
                                item['hwnd'] = current_hwnd
                                self._update_item_state(item, True)
                                # Ask to restore position
                                self.ask_restore_window(current_hwnd, saved_data)
                                break
            
            # Then check current items in the listbox
            for item in list(self.window_listbox.items):
                hwnd = item['hwnd']
                was_active = not item.get('inactive', False)
                is_active = hwnd in current_windows
                
                if was_active != is_active:
                    changes = True
                    self._update_item_state(item, is_active)
            
            # Finally check for any new windows that match saved positions
            for hwnd_str, saved_data in list(saved_positions.items()):
                saved_info = saved_data.get("info", {})
                # Skip if we already have this window in our list
                if any(item['hwnd'] == int(hwnd_str) for item in self.window_listbox.items):
                    continue
                
                # Look for matching window in current_windows
                for current_hwnd, current_info in current_windows.items():
                    if (current_info['process_name'].lower() == saved_info.get('process_name', '').lower() and
                        current_info['title'] == saved_info.get('title', '')):
                        # Found a new match - add to list
                        changes = True
                        self.window_listbox.insert(tk.END, None, 
                                                 current_info['process_name'], 
                                                 current_info['title'], 
                                                 current_hwnd)
                        # Select it since it was saved
                        last_item = self.window_listbox.items[-1]
                        last_item['saved_info'] = saved_data
                        self.window_listbox._on_item_click(last_item)
                        # Ask to restore position
                        self.ask_restore_window(current_hwnd, saved_data)
                        break
        
        finally:
            # Clear the flag
            self._handling_window_close = False

        if changes:
            self.window_listbox.update_idletasks()

    def _update_item_state(self, item, is_active):
        """Update the visual state of a listbox item."""
        if is_active:
            # For active items, use Selected style if selected, otherwise White
            style_suffix = 'Selected.TFrame' if item['index'] in self.window_listbox.selected_indices else 'White.TFrame'
            label_suffix = 'Selected.TLabel' if item['index'] in self.window_listbox.selected_indices else 'White.TLabel'
            bg_color = '#2E8B57' if item['index'] in self.window_listbox.selected_indices else 'white'
            # Remove "(NOT RUNNING)" from process name if it exists
            process_text = item['process_label'].cget('text')
            if "(NOT RUNNING)" in process_text:
                process_text = process_text.replace(" (NOT RUNNING)", "")
                item['process_label'].configure(text=process_text)
        else:
            # For inactive items, always use Inactive style
            style_suffix = 'Inactive.TFrame'
            label_suffix = 'Inactive.TLabel'
            bg_color = '#E8F5E9'  # Very light green
            # Add "(NOT RUNNING)" to process name if not already there
            process_text = item['process_label'].cget('text')
            if "(NOT RUNNING)" not in process_text:
                process_text = f"{process_text} (NOT RUNNING)"
                item['process_label'].configure(text=process_text)
        
        # Update item state
        item['inactive'] = not is_active
        
        # Update styles
        for widget in [item['container'], item['frame'], item['icon_frame'], item['text_frame']]:
            widget.configure(style=style_suffix)
        
        for widget in [item['process_label'], item['title_label']]:
            widget.configure(style=label_suffix)
        
        if item['icon_label']:
            item['icon_label'].configure(bg=bg_color)

    def _debounced_highlight(self, hwnd, selected):
        """Process the highlight after debounce delay."""
        # Cancel any pending highlight for this window
        if hwnd in self.pending_highlights:
            self.root.after_cancel(self.pending_highlights[hwnd])
            del self.pending_highlights[hwnd]

        # Schedule the highlight operation
        current_time = time.time() * 1000  # Convert to milliseconds
        last_time = self.last_selection_time.get(hwnd, 0)
        
        if current_time - last_time > self.debounce_delay:
            self._process_highlight(hwnd, selected)
        else:
            # Schedule the highlight for later
            self.pending_highlights[hwnd] = self.root.after(
                self.debounce_delay,
                lambda: self._process_highlight(hwnd, selected)
            )

    def _process_highlight(self, hwnd, selected):
        """Actually perform the highlight operation."""
        self.last_selection_time[hwnd] = time.time() * 1000
        
        if selected:
            if self.is_focused and hwnd not in self.highlighted_windows:
                self.window_manager.highlight_window(hwnd, True)
                self.highlighted_windows.add(hwnd)
        else:
            if hwnd in self.highlighted_windows:
                self.window_manager.highlight_window(hwnd, False)
                self.highlighted_windows.remove(hwnd)

    def on_window_selection_change(self, hwnd, selected):
        """Handle window selection changes with debouncing."""
        # Find the item for this hwnd
        item = next((item for item in self.window_listbox.items if item['hwnd'] == hwnd), None)
        if not item:
            return

        # If the window is inactive (not running), ignore selection
        if item.get('inactive', False):
            # Revert the selection visually
            if selected:
                self.window_listbox._on_item_click(item)  # Toggle back
            return

        # If window is selected and running, save its position
        if selected and win32gui.IsWindow(hwnd):
            try:
                self.window_manager.save_window_position(hwnd)
                # Save to disk immediately when a window is selected
                self.window_manager.save_positions_to_disk()
            except Exception as e:
                logging.error(f"Failed to save window position: {e}")
        # If window is explicitly deselected by user and is currently running
        elif not selected and win32gui.IsWindow(hwnd) and not hasattr(self, '_handling_window_close'):
            try:
                # Remove from in-memory positions
                if hwnd in self.window_manager.saved_window_positions:
                    del self.window_manager.saved_window_positions[hwnd]
                
                # Remove from saved positions file
                saved_positions = self.window_manager.settings.load_window_positions()
                if str(hwnd) in saved_positions:
                    del saved_positions[str(hwnd)]
                    self.window_manager.settings.save_window_positions(saved_positions)
                    logging.info(f"Removed window {hwnd} from saved positions")
            except Exception as e:
                logging.error(f"Failed to remove window position for {hwnd}: {e}")

        # Normal selection handling for active windows
        self._debounced_highlight(hwnd, selected)
    
    def refresh_windows(self):
        """Update the list of windows in the GUI."""
        self.window_listbox.delete(0, tk.END)
        current_windows = self.window_manager.enum_windows()
        current_hwnds = {hwnd for hwnd, _, _ in current_windows}
        
        # First add saved but not currently running windows at the top
        saved_positions = self.window_manager.settings.load_window_positions()
        for hwnd_str, data in saved_positions.items():
            hwnd = int(hwnd_str)
            if hwnd not in current_hwnds:  # Window is saved but not running
                window_info = data.get("info", {})
                if window_info:  # Only add if we have window info
                    self.window_listbox.insert(tk.END, None, 
                                             window_info.get("process_name", "Unknown"), 
                                             window_info.get("title", "Unknown"), 
                                             hwnd)
                    # Mark as selected and inactive
                    last_item = self.window_listbox.items[-1]
                    last_item['inactive'] = True  # Mark as inactive
                    last_item['saved_info'] = data  # Store saved info for later
                    self._update_item_state(last_item, False)  # Apply inactive style
        
        # Then add all currently running windows
        for hwnd, title, process_name in current_windows:
            # Check if this window was previously saved but inactive
            was_inactive = False
            saved_info = None
            if str(hwnd) in saved_positions:
                saved_info = saved_positions[str(hwnd)]
                # Check if we had this in our inactive list
                for item in self.window_listbox.items:
                    if item.get('hwnd') == hwnd and item.get('inactive', False):
                        was_inactive = True
                        break

            self.window_listbox.insert(tk.END, None, process_name, title, hwnd)
            last_item = self.window_listbox.items[-1]
            last_item['inactive'] = False
            if saved_info:
                last_item['saved_info'] = saved_info
            
            # If it was saved, select it
            if str(hwnd) in saved_positions:
                self.window_listbox._on_item_click(self.window_listbox.items[-1])
                
                # If it was previously inactive, ask about restoration
                if was_inactive:
                    self.ask_restore_window(hwnd, saved_info)
        
        # Then load icons in background
        def load_icons():
            logging.info(f"Starting icon loading for {len(self.window_listbox.items)} windows")
            for i, item in enumerate(self.window_listbox.items):
                try:
                    hwnd = item['hwnd']
                    if hwnd in current_hwnds:  # Only try to get icons for running windows
                        icon = self.window_manager.get_window_icon(hwnd)
                        if icon:
                            logging.info(f"Got icon for window {i} (hwnd: {hwnd})")
                            self.root.after(0, lambda idx=i, ic=icon: self.window_listbox.update_icon(idx, ic))
                except Exception as e:
                    logging.error(f"Failed to load icon for window {i} (hwnd: {hwnd}): {str(e)}")
        
        # Start icon loading thread
        threading.Thread(target=load_icons, daemon=True).start()
    
    def ask_restore_window(self, hwnd, saved_data):
        """Ask user if they want to restore a window that just became active."""
        # Check if we're already handling this window
        if hasattr(self, '_handling_restore') and self._handling_restore.get(hwnd):
            return
        
        # Set handling flag for this window
        if not hasattr(self, '_handling_restore'):
            self._handling_restore = {}
        self._handling_restore[hwnd] = True
        
        try:
            if messagebox.askyesno("Restore Window Position", 
                                     "This window was previously saved. Do you want to restore its position?"):
                # Get the position data from the saved info
                position_data = saved_data.get("position", None)
                if position_data:
                    # Update the in-memory positions first
                    self.window_manager.saved_window_positions[hwnd] = position_data
                    # Then restore using the position data
                    self.window_manager.restore_window_position(hwnd)
                
                # Find the item in the listbox and update its state
                for i, item in enumerate(self.window_listbox.items):
                    if item['hwnd'] == hwnd:
                        item['inactive'] = False
                        self._update_item_state(item, True)  # Mark as active
                        if item['index'] not in self.window_listbox.selected_indices:
                            self.window_listbox._on_item_click(item)  # Select it if not already selected
                        
                        # Get and update the icon
                        try:
                            icon = self.window_manager.get_window_icon(hwnd)
                            if icon:
                                self.window_listbox.update_icon(i, icon)
                        except Exception as e:
                            logging.error(f"Failed to load icon for window {hwnd}: {str(e)}")
                        break
        finally:
            # Clear the handling flag after a delay
            self.root.after(1000, lambda: self._clear_restore_flag(hwnd))
    
    def _clear_restore_flag(self, hwnd):
        """Clear the restore handling flag for a window."""
        if hasattr(self, '_handling_restore'):
            self._handling_restore.pop(hwnd, None)

    def restore_all(self):
        """Restore all saved window positions."""
        if not self.window_manager.saved_window_positions:
            messagebox.showwarning("No Saved Positions", "No window positions have been saved yet.")
            return
            
        # First restore all windows
        for hwnd in self.window_manager.saved_window_positions:
            self.window_manager.restore_window_position(hwnd)
        
        # Give windows time to settle in their new positions
        self.root.after(100, self._update_overlays_after_restore)
    
    def _update_overlays_after_restore(self):
        """Update overlay positions after windows have been restored."""
        # Remove all current overlays
        self.window_manager.cleanup_overlays()
        self.highlighted_windows.clear()
        
        # Re-create overlays for all selected windows if we're focused
        if self.is_focused:
            for item in self.window_listbox.items:
                if item['index'] in self.window_listbox.selected_indices:
                    hwnd = item['hwnd']
                    if win32gui.IsWindow(hwnd):  # Verify window still exists
                        self.window_manager.highlight_window(hwnd, True)
                        self.highlighted_windows.add(hwnd)
    
    def on_monitors_reconnected(self):
        """Called when monitors are reconnected."""
        if self.window_manager.saved_window_positions:
            if messagebox.askyesno("Monitors Detected", 
                                 "Both monitors are now connected. Would you like to restore window positions?"):
                self.restore_all()
    
    def signal_handler(self, signum, frame):
        """Handle system interrupts gracefully."""
        self.on_closing()
    
    def on_closing(self):
        """Clean up before closing."""
        try:
            self.window_manager.cleanup_overlays()  # Clean up overlay windows
            # Don't save positions to disk here as it might overwrite inactive items
            self.window_manager.stop_monitor_check()
            if self.tray_icon:
                self.tray_icon.stop()
            self.root.quit()
            self.root.destroy()
            sys.exit(0)
        except Exception:
            pass  # Ignore any errors during shutdown
    
    def show_settings(self):
        """Show the settings dialog."""
        SettingsDialog(self.root, self.window_manager.settings)
    
    def setup_tray(self):
        """Setup system tray icon and menu."""
        self.tray_icon = None
        self.create_tray_icon()
    
    def create_tray_icon(self):
        """Create the system tray icon."""
        try:
            import pystray
            from PIL import Image

            # Load the custom icon
            icon_path = os.path.join(os.path.dirname(__file__), "greenhouse_icon.png")
            if os.path.exists(icon_path):
                icon_image = Image.open(icon_path)
            else:
                # Fallback to default green square if icon file not found
                icon_image = Image.new('RGB', (64, 64), color='white')
                draw = ImageDraw.Draw(icon_image)
                draw.rectangle([8, 8, 56, 56], fill='green')

            menu = (
                pystray.MenuItem("Show Window", self.show_window),
                pystray.MenuItem("Settings", self.show_settings),
                pystray.Menu.SEPARATOR,
                pystray.MenuItem("Exit", self.on_closing)
            )

            self.tray_icon = pystray.Icon(
                "greenhouse",
                icon_image,
                "Greenhouse",
                menu
            )
            
            # Start the icon in a separate thread
            threading.Thread(target=self.tray_icon.run, daemon=True).start()
            
        except ImportError:
            messagebox.showwarning(
                "Feature Not Available",
                "System tray feature requires 'pystray' package. Install with: pip install pystray"
            )
    
    def minimize_to_tray(self):
        """Minimize the window to system tray."""
        self._on_focus_out(None)  # Remove highlights when minimizing
        self.root.withdraw()  # Hide the window
    
    def show_window(self):
        """Show the window from system tray."""
        self.root.deiconify()  # Show the window
        self.root.lift()  # Bring to front
        self._on_focus_in(None)  # Add highlights when showing
    
    def _on_focus_out(self, event):
        """Handle main window focus loss."""
        if self.is_focused:  # Only act if we were focused before
            self.is_focused = False
            # Remove all window highlights
            for hwnd in list(self.highlighted_windows):
                self.window_manager.highlight_window(hwnd, False)
            self.highlighted_windows.clear()
    
    def _on_focus_in(self, event):
        """Handle main window focus gain."""
        if not self.is_focused:  # Only act if we weren't focused before
            self.is_focused = True
            # Add highlights to all selected windows
            for item in self.window_listbox.items:
                if item['index'] in self.window_listbox.selected_indices:
                    hwnd = item['hwnd']
                    if hwnd not in self.highlighted_windows:  # Only highlight if not already highlighted
                        self.window_manager.highlight_window(hwnd, True)
                        self.highlighted_windows.add(hwnd)

def main():
    """Application entry point."""
    try:
        # Setup logging first
        log_file = setup_logging()
        logging.info(f"Log file created at: {log_file}")
        
        root = tk.Tk()
        app = WindowManagerGUI(root)
        logging.info("Application GUI initialized")
        root.mainloop()
    except KeyboardInterrupt:
        logging.info("Application terminated by user (KeyboardInterrupt)")
    except Exception as e:
        # Print the full error details
        import traceback
        error_details = traceback.format_exc()
        logging.error(f"Unexpected error occurred:\n{error_details}")
        messagebox.showerror("Error", f"An unexpected error occurred:\n{str(e)}\n\nCheck the logs at:\n{log_file}")
    finally:
        logging.info("Application shutting down")
        sys.exit(0)

if __name__ == "__main__":
    main()
