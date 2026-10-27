# 探针脚本: 手工调用 Input.update 看是否生效
# 用法: 由 RGSSEval 执行

$out = []
begin
  $out << "Input.class=#{Input.class}"
  $out << "Input.respond_to?(:update)=#{Input.respond_to?(:update)}"
  $out << "Input::C=#{Input::C.inspect}"
  $out << "Input::DOWN=#{Input::DOWN.inspect}"
  $out << "before dir4=#{Input.dir4.inspect}"
  Input.update
  $out << "after  dir4=#{Input.dir4.inspect}"
  $out << "trigger?(C)=#{Input.trigger?(Input::C).inspect}"
  $out << "press?(C)=#{Input.press?(Input::C).inspect}"
rescue Exception => e
  $out << "EXC #{e.class}: #{e.message}"
end

File.open("input_probe_result.txt", "wb") do |f|
  $out.each { |l| f.puts l }
end
