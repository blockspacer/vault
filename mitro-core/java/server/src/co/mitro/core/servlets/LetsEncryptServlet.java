/*******************************************************************************
 * Copyright (c) 2016, Aquarious, Inc.
 * Authors:
 * Vault Team (team@vaultapp.xyz)
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
 *     You can contact the authors at team@vaultapp.xyz
 *******************************************************************************/
package co.mitro.core.servlets;

import java.io.IOException;
import java.io.StringReader;
import java.lang.reflect.Type;
import java.util.List;
import java.util.regex.Pattern;

import javax.servlet.annotation.WebServlet;
import javax.servlet.http.HttpServlet;
import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.google.common.base.Strings;
import com.google.gson.Gson;
import com.google.gson.JsonSyntaxException;
import com.google.gson.TypeAdapter;
import com.google.gson.reflect.TypeToken;
import com.google.gson.stream.JsonReader;

@WebServlet("/.well-known/acme-challenge/*")
public class LetsEncryptServlet extends HttpServlet {
  private static final Logger logger = LoggerFactory.getLogger(ServerRejectsServlet.class);
  private static final long serialVersionUID = 1L;

  @Override
  protected void doGet(HttpServletRequest request, HttpServletResponse response) throws IOException {
    String acmeChallengeCode = System.getenv("LETS_ENCRYPT_CHALLENGE_CODE");
    if (acmeChallengeCode == null) {
      acmeChallengeCode = "unknown";
    }

    response.getWriter().write(acmeChallengeCode);
  }
}
