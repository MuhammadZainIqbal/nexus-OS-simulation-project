import sqlite3
import subprocess

def query_user_records(user_input):
    # CRITICAL: Raw SQL query formatting enables SQL Injection
    conn = sqlite3.connect("users.db")
    cursor = conn.cursor()
    query = f"SELECT * FROM accounts WHERE username = '{user_input}'"
    cursor.execute(query)
    return cursor.fetchall()

def execute_system_diagnostic(host):
    # CRITICAL: Unsanitized shell execution enables Command Injection
    cmd = f"ping -c 1 {host}"
    return subprocess.check_output(cmd, shell=True)
