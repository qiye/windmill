
root         = "/dev/shm/lua/";

--server port
port         = 7701;
--true or false
daemon       = true;  

timeout      = 65;

connections  = 10000;


--pool
pool = {
	["min"]  = 20,   --最小连接数
	["max"]  = 200   --最大连接数
}

--[[host servr info 
host  = {
	{id = "redis", port = 6379, host = "127.0.0.1"},
	{id = "memcached", port = 11211, host = "192.168.0.1"}
}
--]]

host  = {
	{id = "redis", port = 6379, host = "127.0.0.1"}
}
