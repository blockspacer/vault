/*
 * *****************************************************************************
 * Copyright (c) 2012, 2013, 2014 Lectorius, Inc.
 * Authors:
 * Vijay Pandurangan
 * Evan Jones
 * Adam Hilss
 *
 *
 *     This program is free software: you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation, either version 3 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *     You can contact the authors at team@vaultapp.xyz.
 * *****************************************************************************
 */

var debugMode = true;
var MITRO_HOST = 'localhost';
var MITRO_PORT = 8443;
var MITRO_AGENT_HOST = 'localhost';
var MITRO_AGENT_PORT = 8444;
var FAILOVER_MITRO_HOST = null;
var FAILOVER_MITRO_PORT = null;
var FIREFOX;
var CHROME;
var SAFARI;
var WEBPAGE;
