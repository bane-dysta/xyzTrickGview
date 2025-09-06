# Makefile for XYZ Monitor with Logging
# Use in WSL with mingw-w64

# Compiler settings
CXX = x86_64-w64-mingw32-g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -static-libgcc -static-libstdc++
LDFLAGS = -static -mwindows
LIBS = -luser32 -lgdi32 -lkernel32 -lshell32 -lpthread

# Target settings
TARGET = xyz_monitor.exe
SOURCE = xyz_monitor.cpp

# Build rules
all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) $(LIBS)
	@echo "Build completed: $(TARGET)"

# Debug build
debug: CXXFLAGS += -g -DDEBUG
debug: LDFLAGS = -static
debug: $(TARGET)

# Clean build files
clean:
	rm -f $(TARGET)
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
	@echo "Config template created: config.ini"
	@echo ""
	@echo "Log levels available: DEBUG, INFO, WARNING, ERROR"
	@echo "Set log_level to DEBUG for detailed debugging information"

# Create directory structure
setup:
	mkdir -p logs temp
	@echo "Created logs and temp directories"

# Full setup (dependencies + directories + config)
init: install-deps setup config
	@echo "Full initialization completed"

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
	mkdir -p dist/logs dist/temp
	@echo "# XYZ Monitor" > dist/README.txt
	@echo "1. Configure gview_path in config.ini" >> dist/README.txt
	@echo "2. Run xyz_monitor.exe" >> dist/README.txt
	@echo "3. Press configured hotkey to process XYZ data from clipboard" >> dist/README.txt
	@echo "4. Check logs/ directory for error messages" >> dist/README.txt
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
	@echo "  init        - Full initialization (deps + dirs + config)"
	@echo "  logs        - Show recent log entries"
	@echo "  clear-logs  - Clear all log files"
	@echo "  package     - Create distribution package"
	@echo "  help        - Show this help message"
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

.PHONY: all debug clean install-deps config setup init logs clear-logs package help