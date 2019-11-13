/*
        (c) Copyright  2012 - 2018 Anton Sviridenko
        https://picapica.im

        This program is free software: you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation, version 3.

        This program is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
create table if not exists nodes
                    (
                        address varchar(255) not null,
                        port int not null,
                        last_active int not null,
                        inactive_count int not null,
                        constraint pk primary key (address,port) on conflict replace 
                     );
insert into nodes values("picapica.im", 2233, 0, 0);
insert into nodes values("picapica.im", 2299, 0, 0);  
insert into nodes values("picapica.ge", 2299, 0, 0);
