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
cd $apache_src/modules/proxy/balancers

/usr/local/apr/build-1/libtool --silent --mode=compile gcc -std=gnu99  -g -O2 -pthread -DLINUX -D_REENTRANT -D_GNU_SOURCE -I. -I$apache_src/os/unix -I$apache_src/include -I/usr/local/apr/include/apr-1 -I$apache_src/modules/aaa -I$apache_src/modules/cache -I$apache_src/modules/core -I$apache_src/modules/database -I$apache_src/modules/filters -I$apache_src/modules/ldap -I$apache_src/server -I$apache_src/modules/loggers -I$apache_src/modules/lua -I$apache_src/modules/proxy -I$apache_src/modules/http2 -I$apache_src/modules/session -I$apache_src/modules/ssl -I$apache_src/modules/test -I$apache_src/server -I$apache_src/modules/md -I$apache_src/modules/arch/unix -I$apache_src/modules/dav/main -I$apache_src/modules/generators -I$apache_src/modules/mappers -prefer-pic -c mod_lbmethod_bysomekey.c && touch mod_lbmethod_bysomekey.slo

/usr/local/apr/build-1/libtool --silent --mode=link gcc -std=gnu99  -g -O2 -pthread -o mod_lbmethod_bysomekey.la -rpath /usr/local/apache2/modules -module -avoid-version  mod_lbmethod_bysomekey.lo
