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


$(function() {
  // TODO: this should probably be a chooser.
  mitro.loadOrganizationInfo(function (orgInfo) {
      var selOrgInfo = orgInfo.getSelectedOrganization();
      if (selOrgInfo) {
          $('#secret-org-id').text(selOrgInfo.id.toString());
      }
  }, onBackgroundError);

  $('#is-secure-note').click(function() {
    $('.for-manual').hide();
    $('.for-note').show();
  });
  $('#is-web-password').click(function() {
    $('.for-manual').show();
    $('.for-note').hide();
  });

  
  $('#save-secret-button').click(function() {
    $('.content').hide();
    $('.ugly-message').text('Saving data ...');
    $('.ugly-message').show();

    var dataFromPage = getSecretDataFromPage();
    background.addSecret({}, dataFromPage.clientData, dataFromPage.secretData, function(secretId) {
      var onSuccess = function() {
        helper.setLocation('admin-manage-secret.html?secretId=' + secretId);
      };

      // right now: orgId is always null because the create page doesn't have an org selector
      // TODO: Add an org selector? Once done, this should actually work
      if (dataFromPage.orgId) {
        background.editSiteShares(secretId, [], [], dataFromPage.orgId, onSuccess, onBackgroundError);
      } else {
        onSuccess();
      }
    }, onBackgroundError);
  });
});
