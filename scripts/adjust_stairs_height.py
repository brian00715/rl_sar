#!/usr/bin/env python3
"""
调整 MuJoCo 场景文件中的台阶高度
Adjust stair heights in MuJoCo scene file
"""

import re
import sys

def adjust_stair_height(input_file, output_file, scale_factor=0.5):
    """
    调整台阶高度
    
    Args:
        input_file: 输入的 XML 文件路径
        output_file: 输出的 XML 文件路径  
        scale_factor: 缩放因子（0.5 表示减半）
    """
    with open(input_file, 'r') as f:
        content = f.read()
    
    # 匹配台阶的 geom 元素（y=4.0 和 y=6.0 区域的台阶）
    # 格式: <geom pos="x y z" type="box" size="width depth height" .../>
    
    def replace_stair(match):
        full_match = match.group(0)
        x = float(match.group(1))
        y = float(match.group(2))
        z = float(match.group(3))
        width = float(match.group(4))
        depth = float(match.group(5))
        half_height = float(match.group(6))
        rest = match.group(7)
        
        # 只处理 y=4.0 或 y=6.0 附近的台阶（1.2 <= x <= 3.0）
        if 1.0 <= x <= 3.5 and (3.5 <= y <= 4.5 or 5.5 <= y <= 6.5):
            # 调整高度和 z 位置
            new_half_height = half_height * scale_factor
            new_z = z * scale_factor
            
            return f'<geom pos="{x} {y} {new_z}" type="box" size="{width} {depth} {new_half_height}"{rest}'
        
        return full_match
    
    # 正则表达式匹配台阶 geom
    pattern = r'<geom pos="([\d.]+) ([\d.]+) ([\d.]+)" type="box" size="([\d.]+) ([\d.]+) ([\d.]+)"([^/>]*)/>'
    
    modified_content = re.sub(pattern, replace_stair, content)
    
    with open(output_file, 'w') as f:
        f.write(modified_content)
    
    print(f"✅ 台阶高度已调整为原来的 {scale_factor*100:.0f}%")
    print(f"   输入文件: {input_file}")
    print(f"   输出文件: {output_file}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("用法: python adjust_stairs_height.py <scale_factor>")
        print("示例: python adjust_stairs_height.py 0.5  # 将台阶高度减半")
        sys.exit(1)
    
    scale_factor = float(sys.argv[1])
    
    input_file = "/home/yzy/MyProject/rl_sar/src/rl_sar_zoo/go2_description/mjcf/scene_terrain.xml"
    output_file = "/home/yzy/MyProject/rl_sar/src/rl_sar_zoo/go2_description/mjcf/scene_terrain_modified.xml"
    
    adjust_stair_height(input_file, output_file, scale_factor)
