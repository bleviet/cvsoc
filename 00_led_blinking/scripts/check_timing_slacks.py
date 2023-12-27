#!/usr/bin/env python3
# Import the necessary libraries
import re
import sys

# Read the file
input_file = sys.argv[1]

with open(input_file, 'r') as file:
    content = file.read()

    # Find the worst-case setup slack
    setup_slack_match = re.search(r'Worst-case setup slack is (-?\d+\.\d+)', content)
    setup_slack = float(setup_slack_match.group(1))

    # Find the worst-case hold slack
    hold_slack_match = re.search(r'Worst-case hold slack is (-?\d+\.\d+)', content)
    hold_slack = float(hold_slack_match.group(1))

    # Define color codes
    RED = '\033[91m'
    GREEN = '\033[92m'
    RESET = '\033[0m'

    # Check if there are negative slacks
    if setup_slack < 0 or hold_slack < 0:
        # Print the text in red to emphasize
        print(f'{RED}Negative slacks found!{RESET}')
        for line in content.split('\n'):
            if 'slack is -' in line.lower():
                print(f'{RED}{line}{RESET}')
    else:
        # Print the information in green for positive slacks
        print(f'{GREEN}Timing clean.{RESET}')
        print(f'Worst-case setup slack: {setup_slack}')
        print(f'Worst-case hold slack: {hold_slack}')
