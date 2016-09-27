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

/**
 * Whole this script is a part of the ExtensionHelper.setLocation test.
 * The fact is is being executed means the test is successful.
 * So the only thing it has to do is report the test succeeded.
 * 
 * Also the ExtensionHelper.setLocation is the last one in the extension tests sequence.
 * That's why the script also reports the extension tester finished.
 */

var client = new Client('extension');
var helper = new ExtensionHelper();
helper.bindClient(client);

client.initRemoteCalls('background', ['reportSetLocationSuccess', 'reportExtensionFinished']);
client.reportSetLocationSuccess();
client.reportExtensionFinished();