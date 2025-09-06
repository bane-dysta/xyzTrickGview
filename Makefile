# Makefile for XYZ Monitor with Logging and Custom Icon
# Use in WSL with mingw-w64

# Compiler settings
CXX = x86_64-w64-mingw32-g++
RC = x86_64-w64-mingw32-windres
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -static-libgcc -static-libstdc++
LDFLAGS = -static -mwindows
LIBS = -luser32 -lgdi32 -lkernel32 -lshell32 -lpthread

# Target settings
TARGET = xyz_monitor.exe
SOURCE = xyz_monitor.cpp
RESOURCE = xyz_monitor.rc
RESOURCE_OBJ = xyz_monitor_res.o

# Build rules
all: $(TARGET)

$(TARGET): $(SOURCE) $(RESOURCE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $< $(RESOURCE_OBJ) $(LDFLAGS) $(LIBS)
	@echo "Build completed: $(TARGET)"

# Compile resource file
$(RESOURCE_OBJ): $(RESOURCE)
	@if [ ! -f "gview.ico" ]; then \
		echo "Warning: gview.ico not found! Please place the icon file in the current directory."; \
		echo "The program will use a default system icon if gview.ico is missing."; \
	fi
	$(RC) -o $@ $<
	@echo "Resource compiled: $(RESOURCE_OBJ)"

# Debug build
debug: CXXFLAGS += -g -DDEBUG
debug: LDFLAGS = -static
debug: $(TARGET)

# Clean build files
clean:
	rm -f $(TARGET) $(RESOURCE_OBJ)
	rm -rf logs/
	rm -rf temp/
	@echo "Cleaned build files and directories"

# Install dependencies (run once in WSL)
install-deps:
	sudo apt update
	sudo apt install -y mingw-w64 g++-mingw-w64

# Create config file template with logging options
config:
	@echo "Creating config.ini template with logging support..."
	@echo "hotkey=CTRL+SHIFT+V" > config.ini
	@echo "gview_path=C:\\G16W\\gview.exe" >> config.ini
	@echo "temp_dir=temp" >> config.ini
	@echo "log_file=logs/xyz_monitor.log" >> config.ini
	@echo "log_level=INFO" >> config.ini
	@echo "log_to_console=true" >> config.ini
	@echo "log_to_file=true" >> config.ini
	@echo "wait_seconds=5" >> config.ini
	@echo "# Memory limit in MB for processing (default: 500MB)" >> config.ini
	@echo "max_memory_mb=500" >> config.ini
	@echo "# Optional: set explicit character limit (0 = auto calculate from memory)" >> config.ini
	@echo "max_clipboard_chars=0" >> config.ini
	@echo "Config template created: config.ini"
	@echo ""
	@echo "Log levels available: DEBUG, INFO, WARNING, ERROR"
	@echo "Set log_level to DEBUG for detailed debugging information"

# Create directory structure
setup:
	mkdir -p logs temp
	@echo "Created logs and temp directories"

# Check for required files
check:
	@echo "Checking required files..."
	@if [ -f "$(SOURCE)" ]; then \
		echo "✓ $(SOURCE) found"; \
	else \
		echo "✗ $(SOURCE) missing!"; \
	fi
	@if [ -f "$(RESOURCE)" ]; then \
		echo "✓ $(RESOURCE) found"; \
	else \
		echo "✗ $(RESOURCE) missing!"; \
	fi
	@if [ -f "gview.ico" ]; then \
		echo "✓ gview.ico found"; \
	else \
		echo "⚠ gview.ico missing (will use default icon)"; \
	fi
	@if [ -f "config.ini" ]; then \
		echo "✓ config.ini found"; \
	else \
		echo "⚠ config.ini missing (will be created on first run)"; \
	fi

# Full setup (dependencies + directories + config)
init: install-deps setup config
	@echo "Full initialization completed"
	@echo ""
	@echo "Don't forget to:"
	@echo "1. Place gview.ico in the current directory for custom icon"
	@echo "2. Update gview_path in config.ini"

# Show log file
logs:
	@if [ -f "logs/xyz_monitor.log" ]; then \
		echo "Recent log entries:"; \
		tail -20 logs/xyz_monitor.log; \
	else \
		echo "Log file not found. Run the program first."; \
	fi

# Clear logs
clear-logs:
	rm -rf logs/
	mkdir -p logs
	@echo "Logs cleared"

# Package for distribution
package: all
	@echo "Creating distribution package..."
	mkdir -p dist
	cp $(TARGET) dist/
	cp -f config.ini dist/ 2>/dev/null || echo "No config.ini found, create with 'make config'"
	cp -f gview.ico dist/ 2>/dev/null || echo "No gview.ico found"
	mkdir -p dist/logs dist/temp
	@echo "# XYZ Monitor v1.0" > dist/README.txt
	@echo "Author: Bane Dysta" >> dist/README.txt
	@echo "" >> dist/README.txt
	@echo "Installation:" >> dist/README.txt
	@echo "1. Configure gview_path in config.ini" >> dist/README.txt
	@echo "2. Run xyz_monitor.exe" >> dist/README.txt
	@echo "3. Press configured hotkey to process XYZ data from clipboard" >> dist/README.txt
	@echo "4. Check logs/ directory for error messages" >> dist/README.txt
	@echo "" >> dist/README.txt
	@echo "Feedback:" >> dist/README.txt
	@echo "GitHub: https://github.com/bane-dysta/xyzTrickGview" >> dist/README.txt
	@echo "Forum: http://bbs.keinsci.com/forum.php?mod=viewthread&tid=55596&fromuid=63020" >> dist/README.txt
	@echo "Distribution package created in dist/"

# Help
help:
	@echo "Available targets:"
	@echo "  all         - Build the application (default)"
	@echo "  debug       - Build with debug information"
	@echo "  clean       - Remove build files and directories"
	@echo "  install-deps- Install mingw-w64 dependencies"
	@echo "  config      - Create config.ini template with logging"
	@echo "  setup       - Create logs and temp directories"
	@echo "  check       - Check for required files"
	@echo "  init        - Full initialization (deps + dirs + config)"
	@echo "  logs        - Show recent log entries"
	@echo "  clear-logs  - Clear all log files"
	@echo "  package     - Create distribution package"
	@echo "  help        - Show this help message"
	@echo ""
	@echo "Files needed:"
	@echo "  xyz_monitor.cpp - Main source code"
	@echo "  xyz_monitor.rc  - Resource file (icon and version info)"
	@echo "  gview.ico       - Custom icon file (optional)"
	@echo "  config.ini      - Configuration file (created automatically)"
	@echo ""
	@echo "Configuration options in config.ini:"
	@echo "  hotkey         - Hotkey combination (e.g., CTRL+SHIFT+V)"
	@echo "  gview_path     - Path to GView executable"
	@echo "  temp_dir       - Temporary files directory"
	@echo "  log_file       - Log file path"
	@echo "  log_level      - Logging level (DEBUG, INFO, WARNING, ERROR)"
	@echo "  log_to_console - Enable console logging (true/false)"
	@echo "  log_to_file    - Enable file logging (true/false)"
	@echo "  wait_seconds   - Seconds to wait before deleting temp files"
	@echo "  max_memory_mb  - Memory limit for processing"

.PHONY: all debug clean install-deps config setup check init logs clear-logs package help