# mod_lbmethod_bysomekey
Customerized load balancing algorithm for apache2 (httpd-2.4.34).

# description
Load balancing according to some key in the request header or request query parameters.

One specific key may be corresponding to multiple nodes.
One node may contain multiple keys.

If one key is already dispatched to one node, the next request with the same key may probably be disaptched to that node as well.
However, if the node is extremely more busy than a given value, the request would be dispatched to an idle node or the least busy node.

Hash-bucket data structure is used to store the key and node mapping relation.


# build
cd /home/apache_build/httpd-2.4.34/modules/proxy/balancers

/usr/local/apr/build-1/libtool --silent --mode=compile gcc -std=gnu99  -g -O2 -pthread -DLINUX -D_REENTRANT -D_GNU_SOURCE -I. -I/home/apache_build/httpd-2.4.34/os/unix -I/home/apache_build/httpd-2.4.34/include -I/usr/local/apr/include/apr-1 -I/home/apache_build/httpd-2.4.34/modules/aaa -I/home/apache_build/httpd-2.4.34/modules/cache -I/home/apache_build/httpd-2.4.34/modules/core -I/home/apache_build/httpd-2.4.34/modules/database -I/home/apache_build/httpd-2.4.34/modules/filters -I/home/apache_build/httpd-2.4.34/modules/ldap -I/home/apache_build/httpd-2.4.34/server -I/home/apache_build/httpd-2.4.34/modules/loggers -I/home/apache_build/httpd-2.4.34/modules/lua -I/home/apache_build/httpd-2.4.34/modules/proxy -I/home/apache_build/httpd-2.4.34/modules/http2 -I/home/apache_build/httpd-2.4.34/modules/session -I/home/apache_build/httpd-2.4.34/modules/ssl -I/home/apache_build/httpd-2.4.34/modules/test -I/home/apache_build/httpd-2.4.34/server -I/home/apache_build/httpd-2.4.34/modules/md -I/home/apache_build/httpd-2.4.34/modules/arch/unix -I/home/apache_build/httpd-2.4.34/modules/dav/main -I/home/apache_build/httpd-2.4.34/modules/generators -I/home/apache_build/httpd-2.4.34/modules/mappers -prefer-pic -c mod_lbmethod_bysomekey.c && touch mod_lbmethod_bysomekey.slo

/usr/local/apr/build-1/libtool --silent --mode=link gcc -std=gnu99  -g -O2 -pthread -o mod_lbmethod_bysomekey.la -rpath /usr/local/apache2/modules -module -avoid-version  mod_lbmethod_bysomekey.lo
