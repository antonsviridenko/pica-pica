create table registrations
(
id int unsigned PRIMARY KEY,
t datetime  NOT NULL,
ip_addr int unsigned NOT NULL,
name varchar(64) CHARACTER SET utf8 NOT NULL,
req_pem varchar(4096) NOT NULL,
cert_pem varchar(4096) NOT NULL
)

ENGINE = InnoDB; 
