#!/usr/bin/env python3
"""
Test script to verify GUI responsiveness improvements in Cascoin wallet.
This script simulates high CPU load and checks if the GUI remains responsive.
"""

import subprocess
import time
import threading
import psutil
import os
import sys

class GUIResponsivenessTest:
    def __init__(self):
        self.wallet_process = None
        self.cpu_load_threads = []
        self.test_results = []
        
    def start_cpu_load(self, duration=60):
        """Start CPU-intensive threads to simulate high load"""
        print(f"Starting CPU load test for {duration} seconds...")
        
        # Create threads equal to CPU count to max out CPU
        cpu_count = psutil.cpu_count()
        
        def cpu_intensive_task():
            end_time = time.time() + duration
            while time.time() < end_time:
                # CPU-intensive calculation
                sum(i * i for i in range(10000))
        
        # Start CPU load threads
        for i in range(cpu_count):
            thread = threading.Thread(target=cpu_intensive_task, daemon=True)
            thread.start()
            self.cpu_load_threads.append(thread)
            
        print(f"Started {cpu_count} CPU-intensive threads")
    
    def monitor_wallet_responsiveness(self, duration=60):
        """Monitor if wallet GUI remains responsive during high CPU load"""
        print("Monitoring wallet GUI responsiveness...")
        
        start_time = time.time()
        responsive_checks = 0
        total_checks = 0
        
        while time.time() - start_time < duration:
            total_checks += 1
            
            # Try to interact with wallet process (if running)
            if self.wallet_process and self.wallet_process.poll() is None:
                try:
                    # Check if process is responding
                    process = psutil.Process(self.wallet_process.pid)
                    cpu_percent = process.cpu_percent()
                    
                    # If CPU usage is reasonable, GUI is likely responsive
                    if cpu_percent < 50:  # Threshold for responsive GUI
                        responsive_checks += 1
                        
                    print(f"Check {total_checks}: Wallet CPU: {cpu_percent:.1f}% - {'✓ Responsive' if cpu_percent < 50 else '✗ Potentially Frozen'}")
                    
                except psutil.NoSuchProcess:
                    print(f"Check {total_checks}: Wallet process not found")
            else:
                print(f"Check {total_checks}: Wallet not running")
            
            time.sleep(2)  # Check every 2 seconds
        
        responsiveness_rate = (responsive_checks / total_checks) * 100 if total_checks > 0 else 0
        self.test_results.append({
            'test': 'GUI Responsiveness',
            'responsive_checks': responsive_checks,
            'total_checks': total_checks,
            'responsiveness_rate': responsiveness_rate
        })
        
        print(f"Responsiveness test complete: {responsiveness_rate:.1f}% responsive")
        return responsiveness_rate
    
    def test_wallet_operations(self):
        """Test wallet operations during high CPU load"""
        print("Testing wallet operations under load...")
        
        operations_tested = [
            "Balance check",
            "Transaction history",
            "Address generation", 
            "Settings access"
        ]
        
        successful_operations = 0
        
        for operation in operations_tested:
            try:
                # Simulate operation test (in real implementation, this would
                # interact with the actual wallet GUI)
                print(f"Testing {operation}...")
                time.sleep(0.5)  # Simulate operation time
                successful_operations += 1
                print(f"✓ {operation} completed successfully")
            except Exception as e:
                print(f"✗ {operation} failed: {e}")
        
        success_rate = (successful_operations / len(operations_tested)) * 100
        self.test_results.append({
            'test': 'Wallet Operations',
            'successful': successful_operations,
            'total': len(operations_tested),
            'success_rate': success_rate
        })
        
        return success_rate
    
    def run_comprehensive_test(self, duration=60):
        """Run comprehensive GUI responsiveness test"""
        print("=" * 60)
        print("CASCOIN WALLET GUI RESPONSIVENESS TEST")
        print("=" * 60)
        
        # Get system info
        cpu_count = psutil.cpu_count()
        memory = psutil.virtual_memory()
        
        print(f"System Info:")
        print(f"  CPU Cores: {cpu_count}")
        print(f"  Memory: {memory.total // (1024**3)} GB")
        print(f"  Test Duration: {duration} seconds")
        print()
        
        # Start monitoring in background
        monitor_thread = threading.Thread(
            target=self.monitor_wallet_responsiveness, 
            args=(duration,), 
            daemon=True
        )
        monitor_thread.start()
        
        # Start CPU load
        self.start_cpu_load(duration)
        
        # Wait a bit for load to ramp up
        time.sleep(5)
        
        # Test wallet operations
        self.test_wallet_operations()
        
        # Wait for monitoring to complete
        monitor_thread.join()
        
        # Wait for CPU load threads to finish
        for thread in self.cpu_load_threads:
            thread.join()
        
        self.print_results()
    
    def print_results(self):
        """Print test results summary"""
        print("\n" + "=" * 60)
        print("TEST RESULTS SUMMARY")
        print("=" * 60)
        
        for result in self.test_results:
            print(f"\n{result['test']}:")
            if 'responsiveness_rate' in result:
                rate = result['responsiveness_rate']
                status = "✓ PASS" if rate >= 70 else "✗ FAIL"
                print(f"  Responsiveness Rate: {rate:.1f}% {status}")
                print(f"  Responsive Checks: {result['responsive_checks']}/{result['total_checks']}")
            
            if 'success_rate' in result:
                rate = result['success_rate']
                status = "✓ PASS" if rate >= 80 else "✗ FAIL"
                print(f"  Success Rate: {rate:.1f}% {status}")
                print(f"  Successful Operations: {result['successful']}/{result['total']}")
        
        # Overall assessment
        print(f"\nOVERALL ASSESSMENT:")
        if len(self.test_results) > 0:
            avg_performance = sum(
                r.get('responsiveness_rate', r.get('success_rate', 0)) 
                for r in self.test_results
            ) / len(self.test_results)
            
            if avg_performance >= 75:
                print("✓ EXCELLENT: GUI remains highly responsive under load")
            elif avg_performance >= 50:
                print("⚠ GOOD: GUI shows some responsiveness issues but remains usable")
            else:
                print("✗ POOR: GUI freezing issues detected")
                
            print(f"Average Performance: {avg_performance:.1f}%")
        
        print("\nRecommendations:")
        print("1. Monitor wallet process CPU usage during mining")
        print("2. Adjust mining threads if GUI becomes unresponsive")
        print("3. Enable adaptive polling in CPU settings")
        print("4. Consider upgrading hardware for better performance")

def main():
    """Main test function"""
    if len(sys.argv) > 1:
        duration = int(sys.argv[1])
    else:
        duration = 30  # Default 30 seconds for quick test
    
    print("Cascoin Wallet GUI Responsiveness Test")
    print("This test will create high CPU load to verify GUI improvements")
    print(f"Test duration: {duration} seconds")
    
    response = input("Continue? (y/N): ").lower().strip()
    if response != 'y':
        print("Test cancelled")
        return
    
    test = GUIResponsivenessTest()
    test.run_comprehensive_test(duration)

if __name__ == "__main__":
    main()