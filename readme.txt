 windmill: 基于LUA的 redis、memcache、HandlerSocket、tokyotyrant的异步编程
 
 author blog: http://www.cellphp.com/
 
 QQ group: 62116204
 email: lijinxing@gmail.com
 
-- lua example code 
nicename   = redis.get("redis", "nicename");
name = redis.get("redis", "name");

redis.decr("redis", "flag");

age = redis.get("redis", "age");
str  = string.format("nicename=%s<br />name=%s<br />age=%s<br />",  nicename, name, age);


--redis.del("redis", "id", "name", "age");
sockio.echo(str);